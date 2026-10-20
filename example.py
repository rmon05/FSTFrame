import fstframe as ff

"""
Example usage of what the fstframe dataframe usage would
look like in python. Note that the parameters will be binded
using pybind11 so will look different from the internal C++ 
format, but the general usage for MP2 would look like this
"""

df = ff.read_csv("games.csv")

result = (
    df.filter("points", ">=", 20)
      .select(["player", "points"])
      .limit(100)
      .execute()
)

avg_points = df.aggregate("points", "avg").execute()