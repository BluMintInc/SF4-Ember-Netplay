//! The opening exchange of a latency connection, the guards that own what an
//! opening holds, and the admission budget (MEMBER_LATENCY.md 3.6). Every
//! resource an opening task holds is owned by a guard, so abort, deadline,
//! failure and rejection all clean up the same way, through destructors.
use std::{
    collections::BTreeMap,
    sync::{Arc, Mutex},
    time::Duration,
};

use iroh::{
    Endpoint, EndpointAddr, EndpointId, RelayUrl,
    endpoint::{Connection, ConnectionError, Incoming, RecvStream, SendStream},
};
use tokio::{
    sync::{OwnedSemaphorePermit, watch},
    time::{Instant, timeout_at},
};

use super::{
    authority::{LatencyAuthority, PeerBinding, Tuple},
    endpoint::{LATENCY_ALPN, dial_options},
};

pub(crate) const FRAME_LEN: usize = 24;
const FRAME_MAGIC: &[u8; 4] = b"LTO1";
const FRAME_VERSION: u8 = 1;
/// The acceptor's whole opening, from `endpoint.accept()`.
pub(crate) const ACCEPT_DEADLINE: Duration = Duration::from_secs(3);
/// The dialer's whole opening.
pub(crate) const DIAL_DEADLINE: Duration = Duration::from_secs(10);
/// Inbound openings in progress, each holding a permit until its slot exists.
pub(crate) const MAX_PENDING: usize = 4;
/// Outbound openings in progress.
pub(crate) const MAX_DIALS: usize = 3;
/// Accepted inbound openings per identity inside `REPLACE_WINDOW`.
pub(crate) const REPLACE_BUDGET: usize = 3;
pub(crate) const REPLACE_WINDOW: Duration = Duration::from_secs(60);
/// The close reason of a connection the acceptor would not authorize.
pub(crate) const REFUSED: &[u8] = b"latency refused";

/// `LTO1`, version 1, three reserved bytes, the sender's incarnation and the
/// receiver's.
pub(crate) fn encode_frame(source: u64, target: u64) -> [u8; FRAME_LEN] {
    let mut frame = [0u8; FRAME_LEN];
    frame[..4].copy_from_slice(FRAME_MAGIC);
    frame[4] = FRAME_VERSION;
    frame[8..16].copy_from_slice(&source.to_le_bytes());
    frame[16..24].copy_from_slice(&target.to_le_bytes());
    frame
}

/// The source and target incarnations of a well-formed frame.
pub(crate) fn decode_frame(frame: &[u8; FRAME_LEN]) -> Option<(u64, u64)> {
    if &frame[..4] != FRAME_MAGIC || frame[4] != FRAME_VERSION || frame[5..8] != [0; 3] {
        return None;
    }
    let source = u64::from_le_bytes(frame[8..16].try_into().ok()?);
    let target = u64::from_le_bytes(frame[16..24].try_into().ok()?);
    Some((source, target))
}

/// A connection whose opening has not finished. Dropping it closes the
/// connection unless it was handed on (`transport.rs` PendingConnection).
pub(crate) struct PendingLatencyConnection(Option<Connection>);

impl PendingLatencyConnection {
    pub(crate) fn new(connection: Connection) -> Self {
        Self(Some(connection))
    }

    pub(crate) fn connection(&self) -> &Connection {
        self.0.as_ref().expect("held until handed on")
    }

    /// Closes it as not authorized.
    fn refuse(&self) {
        self.connection().close(1u32.into(), REFUSED);
    }

    fn disarm(mut self) -> Connection {
        self.0.take().expect("held until handed on")
    }
}

impl Drop for PendingLatencyConnection {
    fn drop(&mut self) {
        if let Some(connection) = &self.0 {
            connection.close(1u32.into(), b"latency opening ended");
        }
    }
}

/// An established latency connection, with the opening stream's handles so
/// the acceptor's one remote stream stays taken. Dropping it closes the
/// connection, as `ControlWorker::drop` does.
pub(crate) struct SlotGuard {
    pub(crate) connection: Connection,
    _send: SendStream,
    _recv: RecvStream,
}

impl SlotGuard {
    pub(crate) fn new(
        connection: PendingLatencyConnection,
        send: SendStream,
        recv: RecvStream,
    ) -> Self {
        Self {
            connection: connection.disarm(),
            _send: send,
            _recv: recv,
        }
    }
}

impl Drop for SlotGuard {
    fn drop(&mut self) {
        self.connection.close(0u32.into(), b"latency slot ended");
    }
}

/// An inbound opening waiting for its slot: its identity and attempt token.
pub(crate) struct Pending {
    pub(crate) attempt: u64,
    pub(crate) binding: PeerBinding,
    pub(crate) task: tokio::task::Id,
}

/// The inbound budget, by `(latency ID, incarnation)`.
#[derive(Default)]
pub(crate) struct AdmissionTable {
    /// When each accepted opening was charged, at most `REPLACE_BUDGET`
    /// inside the last `REPLACE_WINDOW`.
    pub(crate) charged: BTreeMap<(EndpointId, u64), Vec<Instant>>,
    /// At most one opening per identity.
    pub(crate) pending: BTreeMap<(EndpointId, u64), Pending>,
    next_attempt: u64,
}

impl AdmissionTable {
    /// Charges one opening for `identity` and marks it pending, or refuses
    /// when its budget is spent or another of its openings is pending.
    pub(crate) fn try_begin(
        table: &Arc<Mutex<AdmissionTable>>,
        identity: (EndpointId, u64),
        binding: PeerBinding,
        task: tokio::task::Id,
        now: Instant,
    ) -> Option<OpeningReservation> {
        let mut guard = lock(table);
        let state = &mut *guard;
        let charged = state.charged.entry(identity).or_default();
        charged.retain(|at| now.saturating_duration_since(*at) < REPLACE_WINDOW);
        if charged.len() >= REPLACE_BUDGET || state.pending.contains_key(&identity) {
            return None;
        }
        charged.push(now);
        state.next_attempt += 1;
        let attempt = state.next_attempt;
        state.pending.insert(
            identity,
            Pending {
                attempt,
                binding,
                task,
            },
        );
        Some(OpeningReservation {
            table: table.clone(),
            identity,
            attempt,
        })
    }

    /// Forgets the identities that are no longer in the roster. Their
    /// pending openings are aborted by the caller.
    pub(crate) fn retain(&mut self, keep: impl Fn(&(EndpointId, u64)) -> bool) {
        self.charged.retain(|identity, _| keep(identity));
        self.pending.retain(|identity, _| keep(identity));
    }
}

pub(crate) fn lock(table: &Mutex<AdmissionTable>) -> std::sync::MutexGuard<'_, AdmissionTable> {
    table
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}

/// A pending mark. Dropping it clears the mark only if it is still this
/// attempt's; the charged timestamp stays.
pub(crate) struct OpeningReservation {
    table: Arc<Mutex<AdmissionTable>>,
    identity: (EndpointId, u64),
    attempt: u64,
}

impl Drop for OpeningReservation {
    fn drop(&mut self) {
        let mut table = lock(&self.table);
        if table
            .pending
            .get(&self.identity)
            .is_some_and(|pending| pending.attempt == self.attempt)
        {
            table.pending.remove(&self.identity);
        }
    }
}

/// An inbound opening that completed, waiting for the actor to install it.
pub(crate) struct InboundOpened {
    pub(crate) binding: PeerBinding,
    pub(crate) local: Tuple,
    pub(crate) revocation: u64,
    pub(crate) reservation: OpeningReservation,
    pub(crate) connection: PendingLatencyConnection,
    pub(crate) send: SendStream,
    pub(crate) recv: RecvStream,
    pub(crate) permit: OwnedSemaphorePermit,
}

/// What an inbound opening task needs besides its connection.
pub(crate) struct AcceptContext {
    pub(crate) authority: watch::Receiver<Arc<LatencyAuthority>>,
    pub(crate) admissions: Arc<Mutex<AdmissionTable>>,
}

/// The acceptor's opening, all under one deadline from `endpoint.accept()`:
/// TLS, the identity check and reservation, the stream, the dialer's frame,
/// then ours.
pub(crate) async fn accept_opening(
    incoming: Incoming,
    permit: OwnedSemaphorePermit,
    context: AcceptContext,
    deadline: Instant,
) -> Option<InboundOpened> {
    timeout_at(deadline, async move {
        let connection = incoming.accept().ok()?.await.ok()?;
        let pending = PendingLatencyConnection::new(connection);
        let remote = pending.connection().remote_id();
        let authority = context.authority.borrow().clone();
        let Some((local, binding)) = authority.binding_for(remote) else {
            pending.refuse();
            return None;
        };
        let Some(reservation) = AdmissionTable::try_begin(
            &context.admissions,
            (remote, binding.incarnation),
            binding,
            tokio::task::id(),
            Instant::now(),
        ) else {
            pending.refuse();
            return None;
        };
        let (mut send, mut recv) = pending.connection().accept_bi().await.ok()?;
        let mut frame = [0u8; FRAME_LEN];
        recv.read_exact(&mut frame).await.ok()?;
        if decode_frame(&frame) != Some((binding.incarnation, local.incarnation)) {
            pending.refuse();
            return None;
        }
        send.write_all(&encode_frame(local.incarnation, binding.incarnation))
            .await
            .ok()?;
        Some(InboundOpened {
            binding,
            local,
            revocation: authority.revocation,
            reservation,
            connection: pending,
            send,
            recv,
            permit,
        })
    })
    .await
    .ok()
    .flatten()
}

/// Why a dial gave no connection.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum DialFailure {
    /// No opening within `DIAL_DEADLINE`.
    Unreachable,
    /// The peer would not authorize us.
    Refused,
}

/// An outbound opening that completed, waiting for the actor to install it.
pub(crate) struct OutboundOpened {
    pub(crate) binding: PeerBinding,
    pub(crate) local: Tuple,
    pub(crate) revocation: u64,
    pub(crate) attempt: u64,
    pub(crate) connection: PendingLatencyConnection,
    pub(crate) send: SendStream,
    pub(crate) recv: RecvStream,
    /// From writing our frame to reading the acceptor's.
    pub(crate) rtt: Duration,
}

/// One dial, decided by the actor at spawn.
pub(crate) struct DialRequest {
    pub(crate) endpoint: Endpoint,
    pub(crate) binding: PeerBinding,
    pub(crate) local: Tuple,
    pub(crate) relay: Option<RelayUrl>,
    pub(crate) revocation: u64,
    pub(crate) attempt: u64,
}

fn refused(reason: Option<ConnectionError>) -> bool {
    matches!(reason, Some(ConnectionError::ApplicationClosed(close)) if close.reason.as_ref() == REFUSED)
}

/// The dialer's opening, all under one deadline: connect, open the stream,
/// write our frame and read the acceptor's, timing the exchange.
pub(crate) async fn dial_opening(
    request: DialRequest,
    deadline: Instant,
) -> Result<OutboundOpened, DialFailure> {
    let DialRequest {
        endpoint,
        binding,
        local,
        relay,
        revocation,
        attempt,
    } = request;
    timeout_at(deadline, async move {
        let mut address = EndpointAddr::new(binding.latency);
        if let Some(relay) = relay {
            address = address.with_relay_url(relay);
        }
        let connecting = endpoint
            .connect_with_opts(address, LATENCY_ALPN, dial_options())
            .await
            .map_err(|_| DialFailure::Unreachable)?;
        let connection = connecting.await.map_err(|_| DialFailure::Unreachable)?;
        let pending = PendingLatencyConnection::new(connection);
        let failure = |pending: &PendingLatencyConnection| {
            if refused(pending.connection().close_reason()) {
                DialFailure::Refused
            } else {
                DialFailure::Unreachable
            }
        };
        let (mut send, mut recv) = match pending.connection().open_bi().await {
            Ok(streams) => streams,
            Err(_) => return Err(failure(&pending)),
        };
        let started = Instant::now();
        if send
            .write_all(&encode_frame(local.incarnation, binding.incarnation))
            .await
            .is_err()
        {
            return Err(failure(&pending));
        }
        let mut frame = [0u8; FRAME_LEN];
        if recv.read_exact(&mut frame).await.is_err() {
            return Err(failure(&pending));
        }
        let rtt = started.elapsed();
        if decode_frame(&frame) != Some((binding.incarnation, local.incarnation)) {
            return Err(DialFailure::Refused);
        }
        Ok(OutboundOpened {
            binding,
            local,
            revocation,
            attempt,
            connection: pending,
            send,
            recv,
            rtt,
        })
    })
    .await
    .unwrap_or(Err(DialFailure::Unreachable))
}
