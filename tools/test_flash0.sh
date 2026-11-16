#!/bin/sh
# Tests for tools/check_flash0.awk, the gate on overriding the size of flash0.
#
# This file exists because that check was written WRONG TWICE and passed review
# both times. A grep for flash0 and "reg =" on one line missed the ordinary
# two-line form; a per-line depth counter then missed the one-line form. Both
# were found by planting the violation and watching the commit succeed.
#
# Four of these cases exist only to prove the check still FIRES, and five only
# to prove it does not cry wolf - including a flash0 node whose own comment
# contains the exact text it blocks, which is what pmc-partitions.dtsi is.
#
#   sh tools/test_flash0.sh
#
# Exit 0 if every case behaves. Any failure names the case.

set -u
here=$(cd "$(dirname "$0")" && pwd)
awkf="$here/check_flash0.awk"
[ -f "$awkf" ] || { echo "missing $awkf" >&2; exit 1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fails=0

# --- cases that MUST be blocked ---------------------------------------------

cat > "$tmp/bad_two_line.dtsi" <<'EOF'
&flash0 {
	reg = <0x08000000 DT_SIZE_M(2)>;
};
EOF

cat > "$tmp/bad_one_line.dtsi" <<'EOF'
&flash0 { size = <DT_SIZE_M(2)>; };
EOF

cat > "$tmp/bad_nodelabel.dtsi" <<'EOF'
flash0: flash@8000000 {
	compatible = "soc-nv-flash";
	size = <DT_SIZE_M(2)>;
};
EOF

cat > "$tmp/bad_after_partitions.dtsi" <<'EOF'
&flash0 {
	partitions {
		boot_partition: partition@0 { reg = <0x0 DT_SIZE_K(256)>; };
	};
	size = <DT_SIZE_M(2)>;
};
EOF

# --- cases that MUST NOT be blocked -----------------------------------------
# A partition's own reg is legitimate and is the normal content of a flash map.

cat > "$tmp/ok_partitions.dtsi" <<'EOF'
&flash0 {
	partitions {
		compatible = "fixed-partitions";
		boot_partition: partition@0 {
			reg = <0x00000000 DT_SIZE_K(256)>;
			label = "mcuboot";
		};
		slot0_partition: partition@40000 {
			reg = <0x00040000 0x00140000>;
			label = "image-0";
		};
	};
};
EOF

cat > "$tmp/ok_other_node.dtsi" <<'EOF'
&qspi_flash {
	partitions {
		slot1_partition: partition@100000 { reg = <0x100000 0x140000>; };
	};
};
EOF

cat > "$tmp/ok_comment_before.dtsi" <<'EOF'
/* flash0's size is deliberately NOT overridden. Setting it to the real 2 MB
 * looks obviously right and silently corrupts every write above 1 MB. A
 * mention of reg = or size = inside a comment is not a property.
 */
&flash0 {
	partitions {
		scratch_partition: partition@10000 { reg = <0x10000 0x30000>; };
	};
};
EOF

cat > "$tmp/ok_comment_inside_starred.dtsi" <<'EOF'
&flash0 {
	/*
	 * Do NOT add  size = <DT_SIZE_M(2)>;  here.
	 * The driver doubles it on dual-core parts, so reg = would lie too.
	 */
	partitions {
		boot_partition: partition@0 { reg = <0x0 DT_SIZE_K(256)>; };
	};
};
EOF

# The one that broke the leading-asterisk assumption.
cat > "$tmp/ok_comment_inside_bare.dtsi" <<'EOF'
&flash0 {
	/*
	Do not write
	size = <DT_SIZE_M(2)>;
	reg = <0x08000000 DT_SIZE_M(2)>;
	here - the driver doubles it.
	*/
	partitions {
		boot_partition: partition@0 { reg = <0x0 DT_SIZE_K(256)>; };
	};
};
EOF

run() {
    name=$1; want=$2
    awk -f "$awkf" "$tmp/$name" >/dev/null 2>&1
    got=$?
    if [ "$got" -ne "$want" ]; then
        echo "FAIL  $name: wanted exit $want, got $got" >&2
        awk -f "$awkf" "$tmp/$name" 2>&1 | sed 's/^/      /' >&2
        fails=$((fails + 1))
    else
        echo "ok    $name"
    fi
}

echo "must be blocked:"
run bad_two_line.dtsi            1
run bad_one_line.dtsi            1
run bad_nodelabel.dtsi           1
run bad_after_partitions.dtsi    1
echo "must be allowed:"
run ok_partitions.dtsi           0
run ok_other_node.dtsi           0
run ok_comment_before.dtsi       0
run ok_comment_inside_starred.dtsi 0
run ok_comment_inside_bare.dtsi  0

# And against the real files, which are the cases that actually matter. All
# are allowed to be absent: this test must be runnable on a machine that has
# neither a Zephyr tree nor any other port checked out. Set
# FLASH0_EXTRA_DTS to a further devicetree file to include it here - the
# sibling port this partition file derives from is the reason that exists.
echo "real files (skipped if absent):"
for real in \
    "$HOME/zephyrproject/zephyr/boards/arduino/portenta_h7/arduino_portenta_h7_stm32h747xx_m7.dts" \
    ${FLASH0_EXTRA_DTS:+"$FLASH0_EXTRA_DTS"} \
    "$here/../pmc-partitions.dtsi"
do
    [ -f "$real" ] || { echo "skip  $(basename "$real") (not present)"; continue; }
    if awk -f "$awkf" "$real" >/dev/null 2>&1; then
        echo "ok    $(basename "$real")"
    else
        echo "FAIL  $(basename "$real") was blocked and should not be" >&2
        awk -f "$awkf" "$real" 2>&1 | sed 's/^/      /' >&2
        fails=$((fails + 1))
    fi
done

if [ "$fails" -ne 0 ]; then
    echo "" >&2
    echo "$fails case(s) failed." >&2
    exit 1
fi
echo ""
echo "all cases behaved."
