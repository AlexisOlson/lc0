# Search-contempt implementation of Leela Chess Zero

This is branched from [v31](https://github.com/LeelaChessZero/lc0/tree/release/0.31) version of Leela Chess Zero (lc0). It implements search-contempt which is a hybrid mcts algorithm that combines the standard puct search (used in alphazero and now in lc0) and thompson sampling. Details of the search algorithm are in the discussions in the [Leela Chess Zero discord](https://discord.com/invite/pKujYxD) server, which can be easily found by searching for the term "search-contempt-node-limit".

This hybrid version of mcts has been proven to gain significant strength in odds chess, a form of handicap chess where the stronger player plays with a piece less (for example a queen, rook or knight less) than that of the opponent.

This branch works best with specialized nets trained for playing odds chess. For example, one instance of config file settings where the search-contempt does really well for queen odds games is below.

UCI parameters for gui (on top of default parameters for lc0)

 "WeightsFile": "lqo_v2.pb.gz"
 
 "Temperature": "0.7"
 
 "TempEndgame": "0.1"
 
 "TempDecayMoves": "5"
 
 "ScLimit": "28"
 

For instructions on setting up the default lc0, refer to the instructions given in readme for v31 version of lc0 linked above. The same instructions apply for running this search-contempt branch as well.

For obtaining the specialized leela queen odds network, refer to the link below,

[https://github.com/notune/LeelaQueenOdds](https://github.com/notune/LeelaQueenOdds)

The main parameter controlling the strength of the hybrid algorithm is the paramter "ScLimit" for UCI or "search-contempt-node-limit" for command-line options paramter. The most straightforward interpretation of this parameter is the upper limit on the number of nodes that the opponent receiving the odds, searches. So the stronger the opponent who is receiving the odds, the higher the value of the "ScLimit" parameter. The default value of this parameter is 1000000000, which is equivalent to the standard puct search used by lc0.

The reason this hybrid algorithm works really well for odds chess is quite simple to comprehend. The odds play using the specialized odds network with the default mcts algorithm, gets better as more nodes are searched, but only upto a certain point, after which the strength degrades since the search outcome or playstyle begins to resemble the strongest engines in regular (non-odds) chess which are weaker in odds chess. Hence, for the default mcts algorithm, the total nodes searched per move has to be limited in order to optimize playing strength for odds chess. Historically this was around 1000 nodes for queen odds, 10,000 nodes for rook odds and 20,000 nodes for knight odds chess.

The reason that the hybrid mcts does not suffer from this effect is that by freezing the distribution of visits for the opponent's play once the nodes searched for the opponent exceeds "search-contempt-node-limit", we maintain the imperfect play expected of the weaker opponent while simultaneously allowing for higher number of visits to be searched by lc0 without sacrificing strength. Thus with the hybrid algorithm, using a certain fixed value of "search-contempt-node-limit" for comparison, the strength of play always increases with more number of nodes searched unlike the default mcts search algorithm.

For the history on the progress over the years of the odds bots from Leela Chess Zero refer to the blogpost below.

[https://lczero.org/blog/](https://lczero.org/blog/)

## Acknowledgements:

Credit to Noah, Naphthalin, marcus98, Hissha and many others from the lc0 discord server for the highly insightful discussions and their contributions towards progress in odds chess for lc0, which served as an inspiration for the hybrid mcts idea, its implementation and also some experimention that I ran and posted in the Leela Chess Zero discord server. Also credit to some other users like Rust who ran independent testing of the strength improvement with search-contempt and helped point out bugs in the code. Also, credit to all the lc0 developers who have painstakingly wrote, optimized, and refined the lc0 to a really high degree of performance which allowed me to run the experiments much faster than would have otherwise been possible with my limited hardware.
