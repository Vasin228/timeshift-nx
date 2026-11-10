#pragma once
struct PPCContext;
namespace timeshift::renderer {
class TilingHost;
// SDK ABI entry adapters only; no global guest hooks or production host owner.
void BeginGuestTiling(PPCContext &context,TilingHost &host);
void EndGuestTiling(PPCContext &context,TilingHost &host);
}
