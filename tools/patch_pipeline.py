import sys
from pathlib import Path

p = Path("upstream/sa2-nx/app/src/renut_engine/gpu/render/pipeline.cpp")
text = p.read_text(encoding="utf-8")

# 1. Remove forced depth disable for zfunc == 7
old_depth = """    const u32 zfunc = (dc >> 4) & 7;
    desc.depthFunction = ConvertCompareFunction(zfunc);
    if (desc.depthEnabled && !desc.depthWriteEnabled && zfunc == 7)
      desc.depthEnabled = false;
    desc.stencilEnabled = dc & 1;"""

new_depth = """    const u32 zfunc = (dc >> 4) & 7;
    desc.depthFunction = ConvertCompareFunction(zfunc);
    desc.stencilEnabled = dc & 1;"""

# 2. Fix alphaToCoverageEnabled
old_a2c = """  desc.alphaToCoverageEnabled = key.alpha_to_coverage != 0 &&
                                !(key.samples == uint32_t(plume::RenderSampleCount::COUNT_1) &&
                                  banjo::renut_gpu::AlphaToMaskTestOn());"""

new_a2c = """  desc.alphaToCoverageEnabled = key.alpha_to_coverage != 0 &&
                                key.samples != uint32_t(plume::RenderSampleCount::COUNT_1);"""

# Normalize \r\n for matching
text_crlf = text.replace("\r\n", "\n")
old_depth_n = old_depth.replace("\r\n", "\n")
new_depth_n = new_depth.replace("\r\n", "\n")
old_a2c_n = old_a2c.replace("\r\n", "\n")
new_a2c_n = new_a2c.replace("\r\n", "\n")

assert old_depth_n in text_crlf, "old_depth not found!"
text_crlf = text_crlf.replace(old_depth_n, new_depth_n, 1)

assert old_a2c_n in text_crlf, "old_a2c not found!"
text_crlf = text_crlf.replace(old_a2c_n, new_a2c_n, 1)

# Preserve CRLF if original had it
if "\r\n" in text:
    final_text = text_crlf.replace("\n", "\r\n")
else:
    final_text = text_crlf

p.write_text(final_text, encoding="utf-8")
print("pipeline.cpp patched successfully!")
