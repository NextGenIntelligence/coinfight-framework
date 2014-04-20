import Data.Char
import Data.List
import Data.List.Split
import Data.Ord

data Coin = Coin {
	faceValue :: Int,
	quantity :: Int
} deriving( Show )

data Game = Game {
	playerCount :: Int,
	turn :: Int,
	tableChange :: [Coin],
	playerChange :: [[Coin]]
} deriving( Show )

deserialiseCoin :: String -> Coin
deserialiseCoin str = Coin {
	faceValue = read $ f,
	quantity = read $ q
} where (q:f:_) = splitOn "x" str

deserialiseChange :: String -> [Coin]
deserialiseChange str = [ x | x <- map deserialiseCoin values, quantity x > 0 ]
	where values = splitOn "," str

deserialiseGame :: [String] -> Game
deserialiseGame (header:table_change:player_change) = Game {
	playerCount = read a, 
	turn = read b,
	tableChange = deserialiseChange table_change,
	playerChange = map deserialiseChange player_change
} where (a:b:_) = words $ header

data Move = Move {
	giveCoin :: Coin,
	takeChange :: [Coin]
} deriving( Show )

serialiseCoin :: Coin -> String
serialiseCoin coin = (show $ quantity coin) ++ "x" ++ (show $faceValue coin)

serialiseChange :: [Coin] -> String
serialiseChange = intercalate ", " . map serialiseCoin

serialiseMove :: Move -> String
serialiseMove move = (show $ faceValue $ giveCoin move) ++ "\n" ++ (serialiseChange $ takeChange move)

currentPlayer :: Game -> Int
currentPlayer game = mod (turn game) (playerCount game)

coinValue :: Coin -> Int
coinValue coin = (faceValue coin) * (quantity coin)

changeValue :: [Coin] -> Int
changeValue = sum . map coinValue

moveValue :: Move -> Int
moveValue move = (changeValue $ takeChange move) - (coinValue $ giveCoin move)

bestMove :: [Move] -> Move
bestMove = maximumBy (comparing moveValue)
    
-- add a coin to a change pool, 
-- increasing the quantity of an existing coin if it's face value matches
addCoin :: Coin -> [Coin] -> [Coin]
addCoin coin [] = [coin]
addCoin coin (x:xs) = 
	if (faceValue x) == (faceValue coin)
	then Coin{ faceValue=(faceValue x), quantity=(quantity x)+(quantity coin) }:xs
	else x:addCoin coin xs

-- Returns change < value from 
takeChangeFrom :: Int -> [Coin] -> [Coin]
takeChangeFrom v [] = []
takeChangeFrom v (x:xs) =
	if (quantity took) < 1
	then (takeChangeFrom v xs)
	else took:takeChangeFrom (v-(coinValue took)) xs
	where took = Coin{ 
		faceValue= (faceValue x), 
		quantity = minimum [(quot (v-1) (faceValue x)), (quantity x)]
	}

-- generates a move for each coin that could be played by the current player
generateMoves :: [Coin] -> [Coin] -> [Move]
generateMoves [] tableChange = []
generateMoves (x:xs) tableChange = Move { 
	giveCoin = Coin{ faceValue = faceValue x, quantity = 1 },
	takeChange = takeChangeFrom (faceValue x) tableChange
} : (generateMoves xs tableChange)

-- Takes a game state, and returns a move
selectMove :: Game -> Move
selectMove game = bestMove $ generateMoves changePool (tableChange game)
	where changePool = (playerChange game) !! (currentPlayer game)

-- Takes game state as a string, and returns a move as a string
processGame :: String -> String
processGame = serialiseMove . selectMove . deserialiseGame . lines

-- IO: reads game state from stdin and returns a move to stdout
main = do
	contents <- getContents
	putStrLn $ processGame contents