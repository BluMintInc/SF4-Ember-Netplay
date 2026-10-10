# Scheduling for ctest --parallel, included only when tests are registered. A
# test not named here shares nothing with another test: its files, pipes and
# named objects carry a pid or random name.

# Every test that binds a loopback UDP port, directly, through GGPO or through
# the helper. Several close a socket and hand its port number to GGPO, which
# binds it a moment later, and a process that binds port 0 in between can be
# given the same port. So none of them run side by side, and a new test that
# binds UDP belongs here.
set(sf4e_udp_port_tests AbortNotice GgpoConfirmedFrame GgpoDisconnectBeforeInput
    GgpoInputGap GgpoOutageTolerance GgpoPendingOutput GgpoSpectatorBacklog
    GgpoSpectatorCapacity GgpoSpectatorHandleControl GgpoUdpValidation)
set_tests_properties(${sf4e_udp_port_tests} PROPERTIES RESOURCE_LOCK loopback_udp_ports)
# The overlay opens settings.lock in the player's own settings folder without
# sharing, and a second opener fails.
set_tests_properties(OverlayReset PROPERTIES RESOURCE_LOCK user_settings)
# The upgrade script refuses while it lists a Launcher, Updater, sf4-net or
# ember-discord process whose path it cannot read, as for a helper that exits
# during the check, so the tests that start sf4-net never run beside it.
set_tests_properties(UpgradeRecoveryPowerShell PROPERTIES RESOURCE_LOCK ember_processes)
if(TEST HelperRust)
    # These start sf4-net, which binds UDP ports of its own.
    set_tests_properties(HelperProcess HelperRust PROPERTIES
        RESOURCE_LOCK "loopback_udp_ports;ember_processes")
    # The runtime also opens the player's settings.lock, as the overlay does.
    set_tests_properties(RuntimeBootstrap PROPERTIES
        RESOURCE_LOCK "loopback_udp_ports;ember_processes;user_settings")
endif()

# Three threads spin while two checks allow a message 50 ms of wall time.
set_tests_properties(OverlayLifecycle PROPERTIES PROCESSORS 3)
set_tests_properties(PresentationSnapshot PROPERTIES PROCESSORS 2)
