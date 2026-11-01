import time
import random
import statistics
import math
import csv
import pandas as pd
import polars as pl
from bisect import bisect_left, bisect_right

# TEMPORARY import
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.append(str(PROJECT_ROOT / "python"))

import fstframe

CSV_PATH = "test1.csv"
REPEATS = 1
TIMESTAMP_MAX = 1000000000
NUM_RECORDS = 1000000
NUM_QUERIES = 100
NUM_QUERY_CATEGORIES = 3

# read CSV into fstframe
def _max_val_query_fstframe(df, timestamp_lo, timestamp_hi):
    start_time = time.perf_counter()

    matching = (
        df.filter("timestamp", ">=", timestamp_lo)
          .filter("timestamp", "<=", timestamp_hi)
          .select(["val"])
          .aggregate("val", "max")
          .execute()
    )

    max_val = matching["max_val"][0] if matching.row_count() > 0 else None

    elapsed_time = time.perf_counter() - start_time
    return max_val, elapsed_time


def query_fstframe(queries, path=CSV_PATH):
    start_time = time.perf_counter()
    df = fstframe.read_csv(path)
    load_end = time.perf_counter()

    results = []

    for lo, hi in queries:
        max_val, elapsed = _max_val_query_fstframe(df, lo, hi)
        results.append(elapsed)

    print("FSTFrame")
    print(f"  Rows: {df.row_count()}")
    print(f"  Queries: {NUM_QUERIES * NUM_QUERY_CATEGORIES}")
    print(f"  Load Time: {1000 * (load_end - start_time):.3f} ms")
    print(f"  Min Query Time: {1000 * min(results):.3f} ms")
    print(f"  Max Query Time: {1000 * max(results):.3f} ms")
    print(f"  Mean Query Time: {1000 * (sum(results) / len(results)):.3f} ms")

# read CSV into pandas
def _max_val_query_pandas(df, timestamp_lo, timestamp_hi):
    start_time = time.perf_counter()
    matching = df.loc[
        (df["timestamp"] >= timestamp_lo)
        & (df["timestamp"] <= timestamp_hi),
        "val",
    ]
    max_val = matching.max() if not matching.empty else None
    elapsed_time = time.perf_counter() - start_time
    return (max_val, elapsed_time)

def query_pandas(queries, path=CSV_PATH):
    start_time= time.perf_counter()
    df = pd.read_csv(
        path,
        dtype={"key": "string", "val": "float64", "timestamp": "int64"},
    )
    load_end = time.perf_counter()
    results = []
    for lo, hi in queries:
        max_val, elapsed = _max_val_query_pandas(df, lo, hi)
        results.append(elapsed)
    print("Pandas")
    print(f"  Rows: {NUM_RECORDS}")
    print(f"  Queries: {NUM_QUERIES*NUM_QUERY_CATEGORIES}")
    print(f"  Load Time: {1000*(load_end-start_time):.3f} ms")
    print(f"  Min Query Time: {1000*min(results):.3f} ms")
    print(f"  Max Query Time: {1000*max(results):.3f} ms")
    print(f"  Mean Query Time: {1000*(sum(results)/len(results)):.3f} ms")

# Read CSV into Polars
def _max_val_query_polars(df, timestamp_lo, timestamp_hi):
    start_time = time.perf_counter()
    matching = df.filter(
        (pl.col("timestamp") >= timestamp_lo)
        & (pl.col("timestamp") <= timestamp_hi)
    ).select("val")
    max_val = matching["val"].max() if matching.height > 0 else None
    elapsed_time = time.perf_counter() - start_time
    return (max_val, elapsed_time)


def query_polars(queries, path=CSV_PATH):
    start_time = time.perf_counter()
    df = pl.read_csv(
        path,
        schema_overrides={
            "key": pl.String,
            "val": pl.Float64,
            "timestamp": pl.Int64,
        },
    )
    load_end = time.perf_counter()
    results = []
    for lo, hi in queries:
        max_val, elapsed = _max_val_query_polars(df, lo, hi)
        results.append(elapsed)
    print("Polars")
    print(f"  Rows: {df.height}")
    print(f"  Queries: {len(queries)}")
    print(f"  Load Time: {1000 * (load_end - start_time):.3f} ms")
    print(f"  Min Query Time: {1000 * min(results):.3f} ms")
    print(f"  Max Query Time: {1000 * max(results):.3f} ms")
    print(f"  Mean Query Time: {1000 * (sum(results) / len(results)):.3f} ms")

# read CSV into sqrt decomp
# have baseline evidence this can be faster first
BLOCK_SIZE = math.floor(math.sqrt(NUM_RECORDS))
NUM_BLOCKS = math.ceil(NUM_RECORDS/BLOCK_SIZE)
def _max_val_query_sqrt(records, blocks, timestamps, timestamp_lo, timestamp_hi):
    start_time = time.perf_counter()
    max_val = float("-inf")

    # IMPLEMENTATION START
    if timestamp_lo > timestamp_hi or not records:
        return (None, time.perf_counter() - start_time)

    # Binary search for the inclusive timestamp range [lo, hi].
    left = bisect_left(timestamps, timestamp_lo)
    right = bisect_right(timestamps, timestamp_hi)  # exclusive

    if left >= right:
        return (None, time.perf_counter() - start_time)

    first_block = left // BLOCK_SIZE
    last_block = (right - 1) // BLOCK_SIZE

    for block_id in range(first_block, last_block + 1):
        block_start = block_id * BLOCK_SIZE
        block_end = min(block_start + BLOCK_SIZE, len(records))  # exclusive

        # Entire block is inside the query range.
        if left <= block_start and block_end <= right:
            max_val = max(max_val, blocks[block_id]["max_val"])

        # Partial block: inspect only matching records.
        else:
            scan_start = max(left, block_start)
            scan_end = min(right, block_end)

            for i in range(scan_start, scan_end):
                max_val = max(max_val, records[i]["val"])
    # IMPLEMENTATION END

    elapsed_time = time.perf_counter() - start_time
    return (max_val, elapsed_time)

def sqrt_decomp(queries, path=CSV_PATH):
    # sqrt decomp on timestamp
    start_time = time.perf_counter()
    records = []
    blocks = [
        {
            "max_val": float("-inf"),
            "min_val": float("inf"),
            "sum_vals": 0.0,
            "count_vals": 0,
        }
        for _ in range(NUM_BLOCKS)
    ]

    # read
    read_start = time.perf_counter()
    with open(path, "r", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            key = row["key"]
            val = float(row["val"])
            timestamp = int(row["timestamp"])

            records.append({
                "key": key,
                "val": val,
                "timestamp": timestamp
            })
    read_end = time.perf_counter()

    # sort by timestamp
    preprocess_start = time.perf_counter()
    records.sort(key=lambda row: row["timestamp"])
    timestamps = [r["timestamp"] for r in records]
    for ind, record in enumerate(records):
        block_id = ind//BLOCK_SIZE
        val = record["val"]

        blocks[block_id]["max_val"] = max(blocks[block_id]["max_val"], val)
        blocks[block_id]["min_val"] = min(blocks[block_id]["min_val"], val)
        blocks[block_id]["sum_vals"] += val
        blocks[block_id]["count_vals"] += 1
    preprocess_end = time.perf_counter()

    # test NUM_QUERIES aggregate queries with a random timestamp range
    # benchmark the min, max, mean, median and total time taken across tests
    results = []
    for lo, hi in queries:
        max_val, elapsed = _max_val_query_sqrt(records, blocks, timestamps, lo, hi)
        results.append(elapsed)


    print("SQRT Decomposition Indexing")
    print(f"  Rows: {NUM_RECORDS}")
    print(f"  Queries: {NUM_QUERIES*NUM_QUERY_CATEGORIES}")
    print(f"  Read Time: {1000*(read_end-read_start):.3f} ms")
    print(f"  Preprocess Time: {1000*(preprocess_end-preprocess_start):.3f} ms")
    print(f"  Load Time: {1000*(preprocess_end-start_time):.3f} ms")
    print(f"  Min Query Time: {1000*min(results):.3f} ms")
    print(f"  Max Query Time: {1000*max(results):.3f} ms")
    print(f"  Mean Query Time: {1000*(sum(results)/len(results)):.3f} ms")


def main():
    narrow_queries = []
    medium_queries = []
    broad_queries = []
    for i in range(NUM_QUERIES):
        narrow_duration = TIMESTAMP_MAX//1000
        medium_duration = TIMESTAMP_MAX//100
        broad_duration = TIMESTAMP_MAX//10
        timestamp_lo_narrow = random.randint(1, TIMESTAMP_MAX-narrow_duration)
        timestamp_lo_medium = random.randint(1, TIMESTAMP_MAX-medium_duration)
        timestamp_lo_broad = random.randint(1, TIMESTAMP_MAX-broad_duration)
        timestamp_hi_narrow = timestamp_lo_narrow+narrow_duration
        timestamp_hi_medium = timestamp_lo_medium+medium_duration
        timestamp_hi_broad = timestamp_lo_broad+broad_duration
        narrow_queries.append((timestamp_lo_narrow, timestamp_hi_narrow))
        medium_queries.append((timestamp_lo_medium, timestamp_hi_medium))
        broad_queries.append((timestamp_lo_broad, timestamp_hi_broad))

    queries = narrow_queries + medium_queries + broad_queries
    random.shuffle(queries)

    # run tests
    sqrt_decomp(queries)
    query_pandas(queries)
    query_polars(queries)
    query_fstframe(queries)

if __name__ == "__main__":
    main()