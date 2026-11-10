/**
 * @file        kernel/xam/xam_net_switch.cpp
 * @brief       Switch (Horizon OS) offline stubs for XAM networking
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license     BSD 3-Clause License
 */

#include <rex/hook.h>
#include <rex/logging.h>

// Networking functions return offline / failure codes on Switch (single player)

REX_EXPORT_STUB_RETURN(__imp__NetDll_WSAStartup, 0);
REX_EXPORT_STUB_RETURN(__imp__NetDll_WSACleanup, 0);
REX_EXPORT_STUB_RETURN(__imp__NetDll_WSAGetLastError, 0);
REX_EXPORT_STUB_RETURN(__imp__NetDll___WSAFDIsSet, 0);

REX_EXPORT_STUB_RETURN(__imp__NetDll_XNetStartup, 0);
REX_EXPORT_STUB_RETURN(__imp__NetDll_XNetGetEthernetLinkStatus, 0); // Not connected
REX_EXPORT_STUB_RETURN(__imp__NetDll_XNetGetTitleXnAddr, 0);
REX_EXPORT_STUB_RETURN(__imp__NetDll_XNetInAddrToXnAddr, 0);
REX_EXPORT_STUB_RETURN(__imp__NetDll_XNetXnAddrToInAddr, 0);
REX_EXPORT_STUB_RETURN(__imp__NetDll_XNetRandom, 0);
REX_EXPORT_STUB_RETURN(__imp__NetDll_XNetQosListen, 0);
REX_EXPORT_STUB_RETURN(__imp__NetDll_XNetQosLookup, 0);
REX_EXPORT_STUB_RETURN(__imp__NetDll_XNetQosRelease, 0);

REX_EXPORT_STUB_RETURN(__imp__NetDll_socket, static_cast<uint32_t>(-1));
REX_EXPORT_STUB_RETURN(__imp__NetDll_closesocket, 0);
REX_EXPORT_STUB_RETURN(__imp__NetDll_bind, static_cast<uint32_t>(-1));
REX_EXPORT_STUB_RETURN(__imp__NetDll_connect, static_cast<uint32_t>(-1));
REX_EXPORT_STUB_RETURN(__imp__NetDll_listen, static_cast<uint32_t>(-1));
REX_EXPORT_STUB_RETURN(__imp__NetDll_accept, static_cast<uint32_t>(-1));
REX_EXPORT_STUB_RETURN(__imp__NetDll_select, 0);
REX_EXPORT_STUB_RETURN(__imp__NetDll_recv, static_cast<uint32_t>(-1));
REX_EXPORT_STUB_RETURN(__imp__NetDll_recvfrom, static_cast<uint32_t>(-1));
REX_EXPORT_STUB_RETURN(__imp__NetDll_send, static_cast<uint32_t>(-1));
REX_EXPORT_STUB_RETURN(__imp__NetDll_sendto, static_cast<uint32_t>(-1));
REX_EXPORT_STUB_RETURN(__imp__NetDll_inet_addr, static_cast<uint32_t>(-1));
REX_EXPORT_STUB_RETURN(__imp__NetDll_getsockname, static_cast<uint32_t>(-1));
REX_EXPORT_STUB_RETURN(__imp__NetDll_getsockopt, static_cast<uint32_t>(-1));
REX_EXPORT_STUB_RETURN(__imp__NetDll_setsockopt, static_cast<uint32_t>(-1));
