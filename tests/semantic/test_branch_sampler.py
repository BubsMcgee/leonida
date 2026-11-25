"""A uniform branch may choose between two literal samplers (two S#s).

Leonida samples through the bindless sampler array: the S# words are read from the SGPR file at the sample site, so a
sampler written in either arm of a uniform branch reaches the single lookup after the join. VIctory (DexNom, commit
ddb7332) has an SSA translator instead and had to bind both immediate samplers, sample with each and select on the
edge; this translator has no descriptor Phi to repair. This test pins the shape down: one sample, one sampler lookup,
and its four S# words are runtime loads of s8..s11 rather than the value of one arm, with both literal S#s written
before the join. Standard library only (spirv-as/spirv-val are used when on PATH); no GPU, no game files.
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

import support

E4 = "; encoding: [0x00,0x00,0x00,0x00]"
E8 = "; encoding: [0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]"

# s1 selects the sampler (uniform). s9..s11 are common; s8 is wrap in one arm and clamp in the other, as in the
# VIctory test. s[8:11] is the S#, s[0:7] the T# from the shader's user SGPRs.
PROGRAM = [
    f"s_mov_b32 s8, 0x380 {E4}",
    f"s_mov_b32 s9, 0x380 {E4}",
    f"s_mov_b32 s10, 0x380 {E4}",
    f"s_mov_b32 s11, 0x380 {E4}",
    f"s_cmp_eq_u32 s1, 0 {E4}",
    f"s_cbranch_scc1 5 {E4}",             # pc 5 -> pc 11 (clamp arm)
    f"s_mov_b32 s8, 0x380 {E4}",          # wrap arm: pc 6..10
    f"s_mov_b32 s9, 0x380 {E4}",
    f"s_mov_b32 s10, 0x380 {E4}",
    f"s_mov_b32 s11, 0x380 {E4}",
    f"s_branch 4 {E4}",                   # pc 10 -> pc 15 (join)
    f"s_mov_b32 s8, 0x3ff {E4}",          # clamp arm: pc 11..14
    f"s_mov_b32 s9, 0x0 {E4}",
    f"s_mov_b32 s10, 0x0 {E4}",
    f"s_mov_b32 s11, 0x0 {E4}",
    f"v_mov_b32_e32 v4, 0.5 {E4}",        # join: pc 15
    f"v_mov_b32_e32 v5, 0.5 {E4}",
    f"image_sample_lz v[0:3], v[4:5], s[0:7], s[8:11] dmask:0xf dim:SQ_RSRC_IMG_2D {E8}",
    f"exp mrt0 v0, v1, v2, v3 done vm {E8}",
    f"s_endpgm {E4}",
]

PAD = "%cffffffff"


def translate():
    return support.translate_lines("ps_branch_sampler", PROGRAM)


def sgpr_of(spv, value):
    """The SGPR index the ids chain through OpLoad/OpAccessChain loads from, or None."""
    for _ in range(8):
        m = re.search(rf"^\s*{re.escape(value)} = (\w+?)(?: %\w+)? (%\w+)$", spv, re.M)
        if not m:
            return None
        op, src = m.groups()
        if op == "OpLoad":
            m = re.search(rf"^\s*{re.escape(src)} = OpAccessChain %\S+ %sgpr (%\w+)$", spv, re.M)
            if not m:
                return None
            n = re.search(rf"^\s*{re.escape(m.group(1))} = OpConstant %uint (\d+)$", spv, re.M)
            return int(n.group(1)) if n else None
        if op not in ("OpBitcast", "OpCopyObject"):
            return None
        value = src
    return None


class BranchSampler(unittest.TestCase):
    def test_one_sample_one_runtime_sampler_lookup(self):
        spv = translate()
        samples = [ln.strip() for ln in spv.splitlines() if "= OpImageSampleExplicitLod" in ln]
        self.assertTrue(samples, "the sample after the join is emitted")
        self.assertEqual(len(re.findall(r"OpAccessChain %_ptr_UC_smp %samplers ", spv)), 1,
                         "the sample indexes the sampler array once")
        # The dmask/format variants share that one loaded sampler.
        self.assertEqual(len({ln.split()[-1] for ln in spv.splitlines() if "= OpSampledImage " in ln}), 1,
                         "every sampled-image variant uses the same sampler")

        # The S# lookup is the padded one (4 words + four 0xFFFFFFFF); its words are loads of s8..s11, not either
        # arm's constant, which is what lets the merged value pick the sampler at run time.
        lookups = [ln.strip() for ln in spv.splitlines()
                   if "= OpFunctionCall %uint %desc_lookup_fn " in ln and ln.rstrip().endswith(PAD)]
        self.assertEqual(len(lookups), 1, "the sampler lookup is one padded desc_lookup call")
        args = lookups[0].split()[5:]
        self.assertEqual(len(args), 8)
        self.assertEqual([sgpr_of(spv, a) for a in args[:4]], [8, 9, 10, 11],
                         "the S# words come from the runtime SGPRs, so a uniform branch may choose the sampler")
        self.assertEqual(args[4:], [PAD] * 4)

    def test_both_arms_write_a_sampler_before_the_join(self):
        spv = translate()
        self.assertRegex(spv, r"OpStore %reg_s8 %c380\b")
        self.assertRegex(spv, r"OpStore %reg_s8 %c3ff\b")
        last_s8 = max(m.start() for m in re.finditer(r"OpStore %reg_s8 %c\w+", spv))
        lookup = spv.index("%desc_lookup_fn", spv.index("%main = OpFunction"))
        sampler_lookup = spv.index(PAD, lookup)
        self.assertGreater(sampler_lookup, last_s8,
                           "the sampler is looked up after both arms have written the S#")

    @unittest.skipUnless(shutil.which("spirv-as") and shutil.which("spirv-val"), "needs spirv-as and spirv-val on PATH")
    def test_module_validates(self):
        spv = translate()
        with tempfile.TemporaryDirectory() as tmp:
            a, b = os.path.join(tmp, "m.spvasm"), os.path.join(tmp, "m.spv")
            with open(a, "w", encoding="utf-8", newline="\n") as f:
                f.write(spv)
            subprocess.run(["spirv-as", "--target-env", "vulkan1.2", a, "-o", b], check=True)
            subprocess.run(["spirv-val", "--target-env", "vulkan1.2", b], check=True)


if __name__ == "__main__":
    unittest.main()
