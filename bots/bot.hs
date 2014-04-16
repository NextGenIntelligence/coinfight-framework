import Data.Char
import Data.List
import Data.List.Split

data Coin = Coin {
	faceValue :: Int,
	quantity :: Int
} deriving( Show )

data Change = Change {
	coins :: [Coin]
} deriving( Show )

data Game = Game {
	playerCount :: Int,
	turn :: Int,
	tableChange :: Change,
	playerChange :: [Change]
} deriving( Show )

deserialiseCoin :: String -> Coin
deserialiseCoin str = Coin {
	faceValue = read $ values !! 1,
	quantity = read $ values !! 0
} where values = splitOn "x" str

deserialiseGame :: [String] -> Game
deserialiseGame l = Game {
	playerCount = read $ game_header !! 0, 
	turn = read $ game_header !! 1,
	tableChange = Change [],
	playerChange = []
} where 
	game_header = words $ head l

processGame :: String -> String
processGame contents = head $ lines contents

main = do
	contents <- getContents
	putStrLn $ processGame contents