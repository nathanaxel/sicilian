# Ready Trader Go

## How To Run

Copy and replace files to Optiver's codebase

```shell
cmake -DCMAKE_BUILD_TYPE=Debug -B build  
cmake --build build --config Debug     
cp build/autotrader* .  
python3 rtg.py run autotrader autotrader1 autotrader2 autotrader3  
```

## Versions
| Name          | Version       | Description     |
| ------------- | ------------- | --------        |
| autotrader          | Default         | Default configuration given by Optiver       |
| autotrader1           | 1.0        | Default configuration without heghing         |
| autotrader2           | 2.0         | Marker-taker strategy: buy / sell ETF based on average gap and s.d. between ETF / Future         |
| autotrader3          | 3.0         | Market-maker strategy: delta neutral         |

