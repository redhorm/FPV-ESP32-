// command_udp.h - UDP command receiver + status replies (receiver -> camera)
#pragma once

// Bind the UDP command socket (port CMD_UDP_PORT).
void udp_begin();

// Poll for inbound command datagrams and act on them. Non-blocking; call often.
void udp_loop();
