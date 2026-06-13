#!/bin/bash
INTERPRETER="./sp"
STABLE_LOG="test_stability.log"
rm -f "$STABLE_LOG"

echo "Running Verification for all .sp files (excluding interactive tests)..." | tee -a "$STABLE_LOG"

# 1. Gather all .sp files, excluding modules/ and known interactive ones
SP_FILES=$(find . -name "*.sp" | grep -v "modules/" | grep -v "test_cli.sp")

for FILE in $SP_FILES; do
    echo "================================================" | tee -a "$STABLE_LOG"
    echo "TESTING: $FILE" | tee -a "$STABLE_LOG"
    
    # Run with Interpreter (default)
    echo "[INTERPRETER]" | tee -a "$STABLE_LOG"
    $INTERPRETER "$FILE" >> "$STABLE_LOG" 2>&1
    INT_CODE=$?
    
    # Run with VM
    echo "[VM]" | tee -a "$STABLE_LOG"
    $INTERPRETER "$FILE" --vm >> "$STABLE_LOG" 2>&1
    VM_CODE=$?
    
    # Run with AOT
    echo "[AOT]" | tee -a "$STABLE_LOG"
    $INTERPRETER "$FILE" --aot >> "$STABLE_LOG" 2>&1
    AOT_CODE=$?
    
    echo "EXIT_CODES: INT=$INT_CODE VM=$VM_CODE AOT=$AOT_CODE" | tee -a "$STABLE_LOG"
done

echo "================================================" | tee -a "$STABLE_LOG"
echo "Verification complete. Results logged to $STABLE_LOG" | tee -a "$STABLE_LOG"
