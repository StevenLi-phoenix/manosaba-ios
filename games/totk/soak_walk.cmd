# tools/soak.py --input for Tears of the Kingdom (plan item 166): Link walked round a square with a jump on each side, so the world keeps
# streaming for hours (start it after the save has loaded: --input-after). From the cliff save he goes over the edge and dives, lands or is
# placed back, which loads the area again.
down LSU
sleep 8000
key A 300
up LSU
down LSR
sleep 8000
key A 300
up LSR
down LSD
sleep 8000
key A 300
up LSD
down LSL
sleep 8000
key A 300
up LSL
sleep 4000
