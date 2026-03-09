#!/usr/bin/env bash
set -euo pipefail

# Configuration
DB="${BENCH_DB:-bench_fk_indexer}"
PSQL="sudo -u postgres psql -X -q -d $DB"
NUM_FKS="${NUM_FKS:-100}"
WARMUP="${WARMUP:-10}"
TABLE_ROWS="${TABLE_ROWS:-0}"  # set >0 to test with data

# ── helpers ──────────────────────────────────────────────────────────

die()  { echo "ERROR: $*" >&2; exit 1; }
info() { echo "==> $*"; }

sql() { $PSQL -c "$1"; }

# Returns statement time in ms via psql \timing
time_sql() {
  $PSQL <<EOF | grep -oP 'Time: \K[0-9]+(\.[0-9]+)?'
\\timing on
$1
EOF
}

setup_parent_table() {
  sql "DROP TABLE IF EXISTS child CASCADE;"
  sql "DROP TABLE IF EXISTS parent CASCADE;"
  sql "CREATE TABLE parent (id serial PRIMARY KEY);"

  if [ "$TABLE_ROWS" -gt 0 ]; then
    sql "INSERT INTO parent (id) SELECT g FROM generate_series(1, $TABLE_ROWS) g;"
  fi
}

create_child_table() {
  local ncols=$1
  local cols=""
  for i in $(seq 1 "$ncols"); do
    cols="${cols}, fk_col_${i} int"
  done
  sql "DROP TABLE IF EXISTS child CASCADE;"
  sql "CREATE TABLE child (id serial PRIMARY KEY ${cols});"

  if [ "$TABLE_ROWS" -gt 0 ]; then
    local insert_cols=""
    local insert_vals=""
    for i in $(seq 1 "$ncols"); do
      insert_cols="${insert_cols}, fk_col_${i}"
      insert_vals="${insert_vals}, (g % $TABLE_ROWS) + 1"
    done
    sql "INSERT INTO child (id ${insert_cols}) SELECT g ${insert_vals} FROM generate_series(1, $TABLE_ROWS) g;"
  fi
}

# ── benchmark runner ─────────────────────────────────────────────────

run_benchmark() {
  local label="$1"
  local total=$NUM_FKS
  local timings=()

  info "$label: creating child table with $total FK columns"
  create_child_table "$total"

  # warmup
  info "$label: warming up ($WARMUP iterations)"
  for i in $(seq 1 "$WARMUP"); do
    sql "ALTER TABLE child ADD CONSTRAINT warmup_fk_${i} FOREIGN KEY (fk_col_${i}) REFERENCES parent(id);"
    sql "ALTER TABLE child DROP CONSTRAINT warmup_fk_${i};"
  done

  # actual benchmark — add FKs starting after warmup columns
  info "$label: benchmarking $total ALTER TABLE ADD FK statements"
  for i in $(seq 1 "$total"); do
    local col="fk_col_${i}"
    local ms
    ms=$(time_sql "ALTER TABLE child ADD CONSTRAINT bench_fk_${i} FOREIGN KEY (${col}) REFERENCES parent(id);")
    timings+=("$ms")
  done

  # compute stats
  local sorted
  sorted=$(printf '%s\n' "${timings[@]}" | sort -n)
  local avg median p95 p99 total_ms

  total_ms=$(printf '%s\n' "${timings[@]}" | awk '{s+=$1} END {printf "%.3f", s}')
  avg=$(printf '%s\n' "${timings[@]}" | awk '{s+=$1} END {printf "%.3f", s/NR}')
  median=$(echo "$sorted" | awk -v n="$total" 'NR==int(n/2)+1 {print}')
  p95=$(echo "$sorted" | awk -v n="$total" 'NR==int(n*0.95)+1 {print}')
  p99=$(echo "$sorted" | awk -v n="$total" 'NR==int(n*0.99)+1 {print}')

  info "$label results:"
  echo "  total:  ${total_ms} ms"
  echo "  avg:    ${avg} ms"
  echo "  median: ${median} ms"
  echo "  p95:    ${p95} ms"
  echo "  p99:    ${p99} ms"

  # dump raw timings to file
  printf '%s\n' "${timings[@]}" > "bench/results_${label// /_}.csv"
  info "Raw timings saved to bench/results_${label// /_}.csv"
}

verify_indexes() {
  local expected=$1
  local actual
  actual=$($PSQL -t -c "SELECT count(*) FROM pg_indexes WHERE tablename = 'child' AND indexname LIKE 'child_fk_col_%_idx';" | tr -d ' ')
  if [ "$actual" -eq "$expected" ]; then
    info "Index check PASSED: $actual/$expected FK indexes created"
  else
    info "Index check FAILED: $actual/$expected FK indexes created"
  fi
}

# ── main ─────────────────────────────────────────────────────────────

info "Creating benchmark database"
sudo -u postgres psql -X -q -d postgres -c "DROP DATABASE IF EXISTS $DB;" 2>/dev/null || true
sudo -u postgres psql -X -q -d postgres -c "CREATE DATABASE $DB;"

setup_parent_table

# ── Run 1: without extension ──
info "Run 1: WITHOUT extension"
sql "DROP EXTENSION IF EXISTS pg_fk_indexer;"
run_benchmark "without_extension"
verify_indexes 0

# ── Run 2: with extension ──
info "Run 2: WITH extension"
setup_parent_table
sql "CREATE EXTENSION IF NOT EXISTS pg_fk_indexer;"
sql "ALTER DATABASE $DB SET session_preload_libraries = 'pg_fk_indexer';"
run_benchmark "with_extension"
verify_indexes "$NUM_FKS"

# ── Run 3: with extension, indexes pre-created (isolates extension overhead only) ──
info "Run 3: WITH extension, pre-created indexes (extension overhead only)"
setup_parent_table
create_child_table "$NUM_FKS"
for i in $(seq 1 "$NUM_FKS"); do
  sql "CREATE INDEX IF NOT EXISTS child_fk_col_${i}_idx ON child (fk_col_${i});"
done
run_benchmark "extension_overhead_only"
verify_indexes "$NUM_FKS"

info "Done. Compare the result files in bench/"
