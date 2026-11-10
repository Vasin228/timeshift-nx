import sys

path = 'upstream/sa2-nx/app/src/renut_engine/gpu/backend/present.cpp'
with open(path, 'rb') as f:
    content = f.read()

target = (b'REXCVAR_DEFINE_BOOL(sa2_renut_gamma_ramp, true, "SA2/Renderer",\r\n'
          b'                    "Renut\'s renderer: apply the game\'s display gamma ramp at scan-out, as the Xbox 360\'s display "\r\n'
          b'                    "controller, the emulated GPU and the native renderer do (off: the frame as rendered, lighter)");')

replacement = (b'#if defined(TIMESHIFT_RECOMP)\r\n'
               b'REXCVAR_DEFINE_BOOL(sa2_renut_gamma_ramp, false, "SA2/Renderer",\r\n'
               b'                    "Renut\'s renderer: apply the game\'s display gamma ramp at scan-out, as the Xbox 360\'s display "\r\n'
               b'                    "controller, the emulated GPU and the native renderer do (off: the frame as rendered, lighter)");\r\n'
               b'#else\r\n'
               b'REXCVAR_DEFINE_BOOL(sa2_renut_gamma_ramp, true, "SA2/Renderer",\r\n'
               b'                    "Renut\'s renderer: apply the game\'s display gamma ramp at scan-out, as the Xbox 360\'s display "\r\n'
               b'                    "controller, the emulated GPU and the native renderer do (off: the frame as rendered, lighter)");\r\n'
               b'#endif')

if target not in content:
    print('Target not found in present.cpp')
    sys.exit(1)

content = content.replace(target, replacement, 1)
with open(path, 'wb') as f:
    f.write(content)
print('Patched present.cpp successfully!')
