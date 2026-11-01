from __future__ import annotations

import csv
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "python"))

import fstframe as ff


def build_demo_csv(path: Path) -> None:
    rows = [
        ["player", "team", "points", "minutes"],
        ["Alice", "A", 30, 90],
        ["Bob", "B", 18, 82],
        ["Cara", "A", 27, 74],
        ["Drew", "C", 34, 96],
        ["Eve", "B", 22, 88],
    ]
    with path.open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerows(rows)


demo_csv = ROOT / "demo_games.csv"
build_demo_csv(demo_csv)

df = ff.read_csv(str(demo_csv))

filtered = (
    df.filter("points", ">=", 25)
      .select(["player", "team", "points"])
      .limit(5)
      .execute()
)
print("Filtered rows:")
print(filtered.to_dict())

average_points = df.aggregate("points", "avg", "average_points").execute()
print("Average points:")
print(average_points.to_dict())

top_players = df.select(["player", "points"]).limit(3).execute()
print("Top players:")
print(top_players.to_dict())

print("Column names:", df.column_names())
print("Row count:", df.row_count())
print("Direct column access:", df.get_column("points"))

if demo_csv.exists():
  demo_csv.unlink()
