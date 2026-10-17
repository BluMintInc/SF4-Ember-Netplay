//! A room's own delay (each fighter plays at the delay they chose): a create
//! asks for it once and the same reading goes to the room's row and to the
//! supervisor, it is written only for such a room in every answer, the room
//! host's report is the room's word, and the row survives a restart.
mod common;

use common::{
    public_rooms::{create_room_with, player, raw_listing, send, start, ticket},
    supervisor::{BUILD, report, report_details, secrets},
};
use ember_protocol::rooms::{RoomAdmission, RoomSummary};
use reqwest::StatusCode;
use serde_json::{Value as Json_, json};

fn request(name: &str, own_delay: Option<Json_>) -> Json_ {
    let mut body = json!({ "name": name, "capacity": 8, "build_id": BUILD });
    if let Some(own_delay) = own_delay {
        body["own_delay"] = own_delay;
    }
    body
}

async fn stored(bridge: &common::Bridge, room_id: &str) -> i64 {
    let room_id = room_id.to_owned();
    bridge
        .state()
        .db
        .read(move |tx| {
            Ok(tx.query_row(
                "SELECT own_delay FROM rooms WHERE room_id = ?1",
                [room_id],
                |row| row.get(0),
            )?)
        })
        .await
        .unwrap()
}

#[tokio::test]
async fn the_request_reaches_the_row_the_supervisor_and_the_creators_answer_once() {
    let (bridge, fake) = start().await;
    let (kate, sam, lee) = (
        player(&bridge, 1).await,
        player(&bridge, 2).await,
        player(&bridge, 3).await,
    );

    let (status, own) = create_room_with(
        &bridge,
        &kate,
        "198.51.100.1",
        request("Own", Some(json!(true))),
    )
    .await;
    assert_eq!(status, StatusCode::CREATED, "{own}");
    assert_eq!(own["own_delay"], true);
    serde_json::from_value::<RoomSummary>(own.clone())
        .unwrap()
        .check()
        .unwrap();
    let own_id = own["room_id"].as_str().unwrap();
    assert_eq!(stored(&bridge, own_id).await, 1);

    let (status, shared) =
        create_room_with(&bridge, &sam, "198.51.100.2", request("Shared", None)).await;
    assert_eq!(status, StatusCode::CREATED, "{shared}");
    // A room with the shared delay is answered exactly as it always was.
    assert!(shared.get("own_delay").is_none(), "{shared}");
    assert_eq!(
        stored(&bridge, shared["room_id"].as_str().unwrap()).await,
        0
    );

    // An explicit false is the shared delay, and is not passed on.
    let (status, explicit) = create_room_with(
        &bridge,
        &lee,
        "198.51.100.3",
        request("Explicit", Some(json!(false))),
    )
    .await;
    assert_eq!(status, StatusCode::CREATED, "{explicit}");
    assert!(explicit.get("own_delay").is_none(), "{explicit}");

    // What the supervisor was asked for is the same reading: the field only for
    // the room that asked for it.
    let sent = fake.lock().unwrap().created.clone();
    assert_eq!(sent.len(), 3);
    assert_eq!(sent[0]["room_id"], own_id);
    assert_eq!(sent[0]["own_delay"], true);
    for shared in &sent[1..] {
        assert!(shared.get("own_delay").is_none(), "{shared}");
    }
}

#[tokio::test]
async fn a_request_that_is_not_a_boolean_is_refused_before_anything_is_made() {
    let (bridge, fake) = start().await;
    let kate = player(&bridge, 1).await;
    for bad in [json!("yes"), json!(1), json!(null), json!([true])] {
        let (status, body) = create_room_with(
            &bridge,
            &kate,
            "198.51.100.1",
            request("Bad", Some(bad.clone())),
        )
        .await;
        assert!(status.is_client_error(), "{bad}: {status} {body}");
    }
    assert!(fake.lock().unwrap().created.is_empty());
    // The refusals did not use up Kate's one open room.
    let (status, body) =
        create_room_with(&bridge, &kate, "198.51.100.1", request("Fine", None)).await;
    assert_eq!(status, StatusCode::CREATED, "{body}");
}

#[tokio::test]
async fn listings_and_admissions_carry_the_field_only_for_an_own_delay_room() {
    let (bridge, fake) = start().await;
    let (kate, sam, lee) = (
        player(&bridge, 1).await,
        player(&bridge, 2).await,
        player(&bridge, 3).await,
    );
    let (_, own) = create_room_with(
        &bridge,
        &kate,
        "198.51.100.1",
        request("Own", Some(json!(true))),
    )
    .await;
    let (_, shared) =
        create_room_with(&bridge, &sam, "198.51.100.2", request("Shared", None)).await;
    let (own_id, shared_id) = (
        own["room_id"].as_str().unwrap().to_owned(),
        shared["room_id"].as_str().unwrap().to_owned(),
    );
    for id in [&own_id, &shared_id] {
        report(&fake, id, 1, 0, &[]);
    }
    ember_bridge::poll_rooms(bridge.state()).await;

    let find = |body: &Json_, id: &str| -> Json_ {
        body["rooms"]
            .as_array()
            .unwrap()
            .iter()
            .find(|room| room["room_id"] == id)
            .unwrap()
            .clone()
    };
    for query in ["", "?detail=1"] {
        let body = raw_listing(&bridge, &lee, query).await;
        assert_eq!(find(&body, &own_id)["own_delay"], true, "{query}");
        assert!(
            find(&body, &shared_id).get("own_delay").is_none(),
            "{query}"
        );
    }
    // The page's listing has no credential and the same rooms.
    let page = common::read(
        bridge
            .client
            .get(bridge.url("/v1/rooms/public"))
            .send()
            .await
            .unwrap(),
    )
    .await
    .1;
    assert_eq!(find(&page, &own_id)["own_delay"], true);
    assert!(find(&page, &shared_id).get("own_delay").is_none());

    // Admission: the room the ticket is for says so, and a shared room's
    // admission is what an older reader parses.
    let (status, admitted) = ticket(&bridge, &lee, "198.51.100.3", &own_id, BUILD).await;
    assert_eq!(status, StatusCode::CREATED, "{admitted}");
    assert_eq!(admitted["room"]["own_delay"], true);
    let parsed: RoomAdmission = serde_json::from_value(admitted).unwrap();
    parsed.check().unwrap();
    assert!(parsed.room.own_delay);
    let (status, admitted) = ticket(&bridge, &lee, "198.51.100.3", &shared_id, BUILD).await;
    assert_eq!(status, StatusCode::CREATED, "{admitted}");
    assert!(admitted["room"].get("own_delay").is_none(), "{admitted}");
    assert!(
        !serde_json::from_value::<RoomAdmission>(admitted)
            .unwrap()
            .room
            .own_delay
    );
}

#[tokio::test]
async fn the_room_hosts_report_is_the_rooms_word_and_a_silent_host_changes_nothing() {
    let (bridge, fake) = start().await;
    let (kate, lee) = (player(&bridge, 1).await, player(&bridge, 9).await);
    let (_, own) = create_room_with(
        &bridge,
        &kate,
        "198.51.100.1",
        request("Own", Some(json!(true))),
    )
    .await;
    let id = own["room_id"].as_str().unwrap().to_owned();
    let listed = |detail: bool| {
        let (bridge, lee, id) = (&bridge, &lee, id.clone());
        async move {
            let body = raw_listing(bridge, lee, if detail { "?detail=1" } else { "" }).await;
            body["rooms"]
                .as_array()
                .unwrap()
                .iter()
                .find(|room| room["room_id"] == id.as_str())
                .map(|room| {
                    room.get("own_delay")
                        .and_then(Json_::as_bool)
                        .unwrap_or(false)
                })
        }
    };

    // A report with no details says nothing about the delay: the request stands.
    report(&fake, &id, 1, 0, &[]);
    ember_bridge::poll_rooms(bridge.state()).await;
    assert_eq!(stored(&bridge, &id).await, 1);
    assert_eq!(listed(false).await, Some(true));

    // A host that reports it confirms it.
    report_details(
        &fake,
        &id,
        json!({ "name": "Own", "capacity": 8, "locked": false, "fighters": [3], "own_delay": true }),
    );
    ember_bridge::poll_rooms(bridge.state()).await;
    assert_eq!(stored(&bridge, &id).await, 1);
    assert_eq!(listed(true).await, Some(true));

    // A host that reports details without it plays the shared delay, whatever
    // the room was created asking for.
    report_details(
        &fake,
        &id,
        json!({ "name": "Own", "capacity": 8, "locked": false, "fighters": [3] }),
    );
    ember_bridge::poll_rooms(bridge.state()).await;
    assert_eq!(stored(&bridge, &id).await, 0);
    assert_eq!(listed(false).await, Some(false));

    // Anything but true is not the host saying it.
    for other in [json!(false), json!("true"), json!(1)] {
        report_details(
            &fake,
            &id,
            json!({ "name": "Own", "capacity": 8, "own_delay": other }),
        );
        ember_bridge::poll_rooms(bridge.state()).await;
        assert_eq!(stored(&bridge, &id).await, 0, "{other}");
    }
}

#[tokio::test]
async fn an_own_delay_room_is_still_one_after_the_bridge_restarts() {
    let (mut bridge, fake) = start().await;
    let kate = player(&bridge, 1).await;
    let (_, own) = create_room_with(
        &bridge,
        &kate,
        "198.51.100.1",
        request("Own", Some(json!(true))),
    )
    .await;
    let id = own["room_id"].as_str().unwrap().to_owned();
    report(&fake, &id, 1, 0, &[]);
    ember_bridge::poll_rooms(bridge.state()).await;
    bridge.restart(secrets()).await;
    ember_bridge::poll_rooms(bridge.state()).await;
    assert_eq!(stored(&bridge, &id).await, 1);
    let (status, body) = send(
        &bridge,
        reqwest::Method::GET,
        kate.token(),
        "198.51.100.1",
        "/v1/rooms",
        None,
    )
    .await;
    assert_eq!(status, StatusCode::OK, "{body}");
    assert_eq!(body["rooms"][0]["own_delay"], true, "{body}");
}
