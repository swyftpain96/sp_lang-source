#!/bin/bash

# Configuration
INTERPRETER="./sp"
FAILURE_DIR="test_failures"
SUMMARY_FILE="test_summary.log"

# Setup
rm -rf "$FAILURE_DIR"
mkdir -p "$FAILURE_DIR"
echo "Test Run Summary - $(date)" > "$SUMMARY_FILE"
echo "------------------------------------------------" >> "$SUMMARY_FILE"

# Find all .sp files, excluding modules/ and specific interactive tests
SP_FILES=$(ls *.sp | grep -v "test_cli.sp" | grep -v "test_net_server.sp")
# Note: test_net_server.sp is excluded as it's interactive (listens on port)

total_tests=0
passed_int=0
passed_vm=0
passed_aot=0

for FILE in $SP_FILES; do
    ((total_tests++))
    echo "Testing $FILE..."
    
    # --- INTERPRETER ---
    output_int=$(timeout 5s $INTERPRETER "$FILE" 2>&1)
    status_int=$?
    if [ $status_int -eq 0 ]; then
        ((passed_int++))
    else
        echo "FAILED (INT): $FILE (Exit $status_int)" >> "$SUMMARY_FILE"
        echo "$output_int" > "$FAILURE_DIR/${FILE}_int.log"
    fi
    
    # --- VM ---
    output_vm=$(timeout 5s $INTERPRETER "$FILE" --vm 2>&1)
    status_vm=$?
    if [ $status_vm -eq 0 ]; then
        ((passed_vm++))
    else
        echo "FAILED (VM): $FILE (Exit $status_vm)" >> "$SUMMARY_FILE"
        echo "$output_vm" > "$FAILURE_DIR/${FILE}_vm.log"
    fi
    
    # --- AOT ---
    output_aot=$(timeout 15s $INTERPRETER "$FILE" --aot 2>&1)
    status_aot=$?
    if [ $status_aot -eq 0 ]; then
        ((passed_aot++))
    else
        echo "FAILED (AOT): $FILE (Exit $status_aot)" >> "$SUMMARY_FILE"
        echo "$output_aot" > "$FAILURE_DIR/${FILE}_aot_build.log"
    fi
done

echo "------------------------------------------------" >> "$SUMMARY_FILE"
echo "TOTAL TESTS: $total_tests" >> "$SUMMARY_FILE"
echo "PASSED (INT): $passed_int / $total_tests" >> "$SUMMARY_FILE"
echo "PASSED (VM):  $passed_vm / $total_tests" >> "$SUMMARY_FILE"
echo "PASSED (AOT): $passed_aot / $total_tests" >> "$SUMMARY_FILE"

cat "$SUMMARY_FILE"
