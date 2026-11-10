import re

path = 'private/sdk/src/ui/switch/rex_app_switch.cpp'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

new_clocks = '''    // CPU (1785 MHz: safe high-performance clock for Switch)
    ClkrstSession cpu_session{};
    if (R_SUCCEEDED(clkrstOpenSession(&cpu_session, static_cast<PcvModuleId>(PcvModule_CpuBus), 3))) {
      clocks_read = R_SUCCEEDED(clkrstGetClockRate(&cpu_session, &cpu_hz)) || clocks_read;
      const Result set_rc = clkrstSetClockRate(&cpu_session, 1785000000u);
      clocks_set = R_SUCCEEDED(set_rc) || clocks_set;
      clkrstGetClockRate(&cpu_session, &cpu_hz);
      clkrstCloseSession(&cpu_session);
    }
    // GPU (768 MHz: safe high-performance clock for GM20B)
    ClkrstSession gpu_session{};
    if (R_SUCCEEDED(clkrstOpenSession(&gpu_session, static_cast<PcvModuleId>(PcvModule_GPU), 3))) {
      clocks_read = R_SUCCEEDED(clkrstGetClockRate(&gpu_session, &gpu_hz)) || clocks_read;
      const Result set_rc = clkrstSetClockRate(&gpu_session, 768000000u);
      clocks_set = R_SUCCEEDED(set_rc) || clocks_set;
      clkrstGetClockRate(&gpu_session, &gpu_hz);
      clkrstCloseSession(&gpu_session);
    }
    // EMC (1600 MHz memory bus: full memory bandwidth)
    ClkrstSession emc_session{};
    if (R_SUCCEEDED(clkrstOpenSession(&emc_session, static_cast<PcvModuleId>(PcvModule_EMC), 3))) {
      clkrstSetClockRate(&emc_session, 1600000000u);
      clkrstCloseSession(&emc_session);
    }'''

pattern_clocks = re.compile(r'    // CPU.*?clkrstCloseSession\(&emc_session\);\s*\}\s*\}', re.DOTALL)
m = pattern_clocks.search(text)
if m:
    text = text[:m.start()] + new_clocks + text[m.end():]
    print('Clocks patched successfully')
else:
    print('Clocks pattern not matched')

old_load = '''  if (std::filesystem::exists(resolved_defaults_.config_path)) {
    rex::cvar::LoadConfig(resolved_defaults_.config_path);
  }'''

new_load = '''  if (std::filesystem::exists(resolved_defaults_.config_path)) {
    rex::cvar::LoadConfig(resolved_defaults_.config_path);
    REXLOG_INFO("[config] Loaded configuration from '{}'", resolved_defaults_.config_path.string());
  } else {
    REXLOG_WARN("[config] Configuration file not found at '{}'", resolved_defaults_.config_path.string());
  }'''

if old_load in text:
    text = text.replace(old_load, new_load)
    print('Config load patched successfully')
else:
    print('Config load pattern not matched')

with open(path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(text)
print('Done patching rex_app_switch.cpp')
