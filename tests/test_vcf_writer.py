import pathlib
from io import StringIO

import pytest
from cyvcf2 import VCF
from numpy.testing import assert_array_equal

from vcztools.vcf_writer import write_vcf

from .utils import assert_vcfs_close, vcz_path_cache


@pytest.mark.parametrize("output_is_path", [True, False])
def test_write_vcf(tmp_path, output_is_path):
    original = pathlib.Path("tests/data/vcf") / "sample.vcf.gz"
    vcz = vcz_path_cache(original)
    output = tmp_path.joinpath("output.vcf")

    if output_is_path:
        write_vcf(vcz, output)
    else:
        output_str = StringIO()
        write_vcf(vcz, output_str)
        with open(output, "w") as f:
            f.write(output_str.getvalue())

    v = VCF(output)

    assert v.samples == ["NA00001", "NA00002", "NA00003"]

    variant = next(v)

    assert variant.CHROM == "19"
    assert variant.POS == 111
    assert variant.ID is None
    assert variant.REF == "A"
    assert variant.ALT == ["C"]
    assert variant.QUAL == pytest.approx(9.6)
    assert variant.FILTER is None

    assert variant.genotypes == [[0, 0, True], [0, 0, True], [0, 1, False]]

    assert_array_equal(
        variant.format("HQ"),
        [[10, 15], [10, 10], [3, 3]],
    )

    # check headers are the same
    assert_vcfs_close(original, output)


# fmt: off
@pytest.mark.parametrize(
    ("regions", "targets", "expected_chrom_pos"),
    [
        # regions only
        ("19", None, [("19", 111), ("19", 112)]),
        ("19:112", None, [("19", 112)]),
        ("20:1230236-", None, [("20", 1230237), ("20", 1234567), ("20", 1235237)]),
        ("20:1230237-", None, [("20", 1230237), ("20", 1234567), ("20", 1235237)]),
        ("20:1230238-", None, [("20", 1234567), ("20", 1235237)]),
        ("20:1230237-1235236", None, [("20", 1230237), ("20", 1234567)]),
        ("20:1230237-1235237", None, [("20", 1230237), ("20", 1234567), ("20", 1235237)]),  # noqa: E501
        ("20:1230237-1235238", None, [("20", 1230237), ("20", 1234567), ("20", 1235237)]),  # noqa: E501
        ("19,X", None, [("19", 111), ("19", 112), ("X", 10)]),
        ("X:11", None, [("X", 10)]),  # note differs from targets

        # targets only
        (None, "19", [("19", 111), ("19", 112)]),
        (None, "19:112", [("19", 112)]),
        (None, "20:1230236-", [("20", 1230237), ("20", 1234567), ("20", 1235237)]),
        (None, "20:1230237-", [("20", 1230237), ("20", 1234567), ("20", 1235237)]),
        (None, "20:1230238-", [("20", 1234567), ("20", 1235237)]),
        (None, "20:1230237-1235236", [("20", 1230237), ("20", 1234567)]),
        (None, "20:1230237-1235237", [("20", 1230237), ("20", 1234567), ("20", 1235237)]),  # noqa: E501
        (None, "20:1230237-1235238", [("20", 1230237), ("20", 1234567), ("20", 1235237)]),  # noqa: E501
        (None, "19,X", [("19", 111), ("19", 112), ("X", 10)]),
        (None, "X:11", []),
        (None, "^19,20:1-1234567", [("20", 1235237), ("X", 10)]),  # complement

        # regions and targets
        ("20", "^20:1110696-", [("20", 14370), ("20", 17330)])
    ]
)
# fmt: on
def test_write_vcf__regions(tmp_path, regions, targets,
                            expected_chrom_pos):

    original = pathlib.Path("tests/data/vcf") / "sample.vcf.gz"
    vcz = vcz_path_cache(original)
    output = tmp_path.joinpath("output.vcf")

    write_vcf(vcz, output, variant_regions=regions,
              variant_targets=targets)

    v = VCF(output)
    variants = list(v)
    assert len(variants) == len(expected_chrom_pos)

    assert v.samples == ["NA00001", "NA00002", "NA00003"]

    for variant, chrom_pos in zip(variants, expected_chrom_pos):
        chrom, pos = chrom_pos
        assert variant.CHROM == chrom
        assert variant.POS == pos

@pytest.mark.parametrize(
    ("samples", "expected_genotypes"),
    [
        ("NA00001", [[0, 0, True]]),
        ("NA00001,NA00003", [[0, 0, True], [0, 1, False]]),
        ("NA00003,NA00001", [[0, 1, False], [0, 0, True]]),
    ]
)
def test_write_vcf__samples(tmp_path, samples, expected_genotypes):
    original = pathlib.Path("tests/data/vcf") / "sample.vcf.gz"
    vcz = vcz_path_cache(original)
    output = tmp_path.joinpath("output.vcf")

    write_vcf(vcz, output, samples=samples)

    v = VCF(output)

    assert v.samples == samples.split(",")

    variant = next(v)

    assert variant.CHROM == "19"
    assert variant.POS == 111
    assert variant.ID is None
    assert variant.REF == "A"
    assert variant.ALT == ["C"]
    assert variant.QUAL == pytest.approx(9.6)
    assert variant.FILTER is None

    assert variant.genotypes == expected_genotypes


def test_write_vcf__header_flags(tmp_path):
    original = pathlib.Path("tests/data/vcf") / "sample.vcf.gz"
    vcz = vcz_path_cache(original)
    output = tmp_path.joinpath("output.vcf")

    output_header = StringIO()
    write_vcf(vcz, output_header, header_only=True)

    output_no_header = StringIO()
    write_vcf(vcz, output_no_header, no_header=True)
    assert not output_no_header.getvalue().startswith("#")

    # combine outputs and check VCFs match
    output_str = output_header.getvalue() + output_no_header.getvalue()
    with open(output, "w") as f:
        f.write(output_str)
    assert_vcfs_close(original, output)


@pytest.mark.skip(reason="Setting a header to control output fields is not supported.")
def test_write_vcf__set_header(tmp_path):
    original = pathlib.Path("tests/data/vcf") / "sample.vcf.gz"
    vcz = vcz_path_cache(original)
    output = tmp_path.joinpath("output.vcf")

    # specified header drops NS and HQ fields,
    # and adds H3 and GL fields (which are not in the data)
    vcf_header = """##fileformat=VCFv4.3
##INFO=<ID=AN,Number=1,Type=Integer,Description="Total number of alleles in called genotypes">
##INFO=<ID=AC,Number=.,Type=Integer,Description="Allele count in genotypes, for each ALT allele, in the same order as listed">
##INFO=<ID=DP,Number=1,Type=Integer,Description="Total Depth">
##INFO=<ID=AF,Number=.,Type=Float,Description="Allele Frequency">
##INFO=<ID=AA,Number=1,Type=String,Description="Ancestral Allele">
##INFO=<ID=DB,Number=0,Type=Flag,Description="dbSNP membership, build 129">
##INFO=<ID=H2,Number=0,Type=Flag,Description="HapMap2 membership">
##INFO=<ID=H3,Number=0,Type=Flag,Description="HapMap3 membership">
##FILTER=<ID=s50,Description="Less than 50% of samples have data">
##FILTER=<ID=q10,Description="Quality below 10">
##FORMAT=<ID=GT,Number=1,Type=String,Description="Genotype">
##FORMAT=<ID=GQ,Number=1,Type=Integer,Description="Genotype Quality">
##FORMAT=<ID=DP,Number=1,Type=Integer,Description="Read Depth">
##FORMAT=<ID=GL,Number=G,Type=Float,Description="Genotype likelihoods">
#CHROM	POS	ID	REF	ALT	QUAL	FILTER	INFO	FORMAT	NA00001	NA00002	NA00003
"""  # noqa: E501

    write_vcf(vcz, output, vcf_header=vcf_header)

    v = VCF(output)
    # check dropped fields are not present in VCF header
    assert "##INFO=<ID=NS" not in v.raw_header
    assert "##FORMAT=<ID=HQ" not in v.raw_header
    # check added fields are present in VCF header
    assert "##INFO=<ID=H3" in v.raw_header
    assert "##FORMAT=<ID=GL" in v.raw_header
    count = 0
    for variant in v:
        # check dropped fields are not present in VCF data
        assert "NS" not in dict(variant.INFO).keys()
        assert "HQ" not in variant.FORMAT
        # check added fields are not present in VCF data
        assert "H3" not in dict(variant.INFO).keys()
        assert "GL" not in variant.FORMAT

        assert variant.genotypes is not None
        count += 1
    assert count == 9
