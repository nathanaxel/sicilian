# Ready Trader Go

## Result

Top 32 out of 1053 submissions (97th percentile)

## How To Run

Copy and replace files to Optiver's codebase

```shell
cmake -DCMAKE_BUILD_TYPE=Debug -B build  
cmake --build build --config Debug     
cp build/autotrader* .  
python3 rtg.py run autotrader3_6 
```

## Versions
| Name          | Version       | Description     |
| ------------- | ------------- | --------        |
| autotrader          | Default         | Default configuration given by Optiver       |
| autotrader1           | 1.0        | Default configuration without heghing         |
| autotrader2           | 2.0         | Marker-taker strategy: buy / sell ETF based on average gap and s.d. between ETF / Future         |
| autotrader3          | 3.0         | Market-maker strategy: delta neutral         |
| autotrader4          | 4.0         | Market-maker strategy: Ichimoku        |


