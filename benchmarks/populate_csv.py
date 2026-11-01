import csv
import random

NUM_RECORDS = 1000000
CSV_FILE = "test1.csv"
def main():
    rows = []
    for i in range(NUM_RECORDS):
        rows.append({
            "key": i,
            "val": random.randint(1, 100000),
            "timestamp": random.randint(1, 1000000000)
        })

    with open(CSV_FILE, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["key", "val", "timestamp"])
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()