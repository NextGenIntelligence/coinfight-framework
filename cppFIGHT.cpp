// cppFIGHT v1.9a 
// An AI tournament Framework for "FIGHT!", a free game from Cheapass Games
// (c) T.Frogley 2002-2013
// With thanks also to Oscar Cooper
//
// See cppFIGHT.h for details
//
// Email bug reports to: codemonkey.uk@gmail.com
//

//
// TODO:
//
//  Output HTML/Other formats
//

//disable MSVC++ long symbols warning
#ifdef _MSC_VER 
	
	#pragma warning( disable : 4786 )

	#ifdef max
		#undef max
	#endif
	#ifdef min
		#undef min
	#endif

	namespace std
	{
		template <class T>
		const T& max (const T& a, const T& b)
		{
			return (a > b ? a : b);
		}

		template <class T>
		const T& min (const T& a, const T& b)
		{
			return (a < b ? a : b);
		}
	}

#endif

#include "cppFIGHT.h"

#include <algorithm>	//for std::sort

#include <map>			//for player UID -> score lookup

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>

#include <sys/times.h>
#include <unistd.h>

#include "pplayer.h"

bool gVerbose = false;
int gGamesPerMatch=3;
int gSecondsPerGame=30;

std::string gTspec="re"; // Round Robin and Elimination
std::string gOspec="t";	 // Output spec: t = text, j = json

clock_t ticks_per_s = sysconf(_SC_CLK_TCK);

extern "C" clock_t CFIGHT_GetPlayerTicksPerGame()
{
	static const clock_t ticks = sysconf(_SC_CLK_TCK);
	return ticks*gSecondsPerGame;
}

void Split(const std::string& in, char c, std::vector<std::string>* pOut);

//implementation
namespace CPPFight {

	namespace CoinArray {
		inline int TotalValue( const unsigned int * coins ){
			int result = 0;
			for(int i=0;i!=CFIGHT_COIN_COUNT;++i){
				result += CPPFight::COINLIST[i]*coins[i];
			}
			return result;
		}

		inline int TotalCount( const unsigned int * coins ){
			int result = 0;
			for(int i=0;i!=CFIGHT_COIN_COUNT;++i){
				result += coins[i];
			}
			return result;
		}

		inline unsigned int RemoveCoins( unsigned int * coins, CFIGHT_Coin c, unsigned int n ){
			if (coins[CPPFight::GetCoinIndex(c)] >= n ){
				coins[CPPFight::GetCoinIndex(c)] -= n;
				return n;
			}
			else{
				return 0;
			}
		}

		inline void InsertCoins( unsigned int * coins, const unsigned int * other ){
			for(int i=0;i!=CFIGHT_COIN_COUNT;++i){
				coins[i] += other[i];
			}
		}
		//remove coins from change,
		//returns number of coins removed, does not remove partial amounts (all or nothing).
		inline int RemoveCoins( unsigned int * coins, const unsigned int * other ){
			int i;

			//first pass, test if operation is legal
			for(i=0;i!=CFIGHT_COIN_COUNT;++i){
				if (coins[i] < other[i])
					return 0;
			}

			//second pass
			for(i=0;i!=CFIGHT_COIN_COUNT;++i){
				coins[i] -= other[i];
				assert(coins[i]>=0);
			}

			return TotalCount(other);
		}

	}

	// gets the index of the specified coin ----------------------------------
	class CoinException : public Exception 
	{
		public:
		CoinException(Coin c) {
			char buffer[256];
			sprintf(buffer,"%i is not a valid Coin.", c);
			mError=buffer;
		}
		virtual std::string ToString() const { return mError; }
		std::string mError;
	};
	
	inline int GetCoinIndex( const Coin coinType )
	{
		switch(coinType){
			case PENNY:
				return 0;
			case NICKEL:
				return 1;
			case DIME:
				return 2;
			case QUARTER:
				return 3;
			default:
				throw CoinException(coinType);		
		}
	}

	//
	// Player Register (singleton)
	//

	//player list type, just pointers
	typedef std::vector<Player*> PlayerList;

	class PlayerRegister {
		public:
			static PlayerRegister& Instance() 
			{
				static PlayerRegister instance;
				return instance;
			}

			void RegisterPlayer(Player* player)
			{
				//!!! TODO check for "badness"
				myRegisteredPlayers.push_back(player);
				myActivePlayers.push_back(player);
			}

			void Exclude(std::string& match)
			{
				PlayerList filtered;
				filtered.reserve(myActivePlayers.size());
				for(PlayerList::iterator i = myActivePlayers.begin(); 
					i!=myActivePlayers.end(); ++i) {
					if ((*i)->GetTitle()!=match && (*i)->GetAuthor()!=match)
						filtered.push_back(*i);
				}
				myActivePlayers.swap(filtered);
			}
			
			// get all players registered at time of calling
			const PlayerList& GetPlayerList () const {
				return myActivePlayers;
			}

		private:
			PlayerRegister()
			{}
			
			PlayerList myRegisteredPlayers;
			PlayerList myActivePlayers;
	};

	//
	// class Change
	//
	
	class ChangeException : public Exception 
	{
		public:
		ChangeException(Coin c, int n) {
			char buffer[256];
			sprintf(buffer,"Change error removing: %ix%i\n", n,c);
			mError=buffer;
		}
		ChangeException(const Change& change)
		{
			char buffer[256];
			sprintf(buffer,"Change error removing: %ix%i, %ix%i, %ix%i, %ix%i",
				change.GetCount(PENNY),PENNY,
				change.GetCount(NICKEL),NICKEL,
				change.GetCount(DIME),DIME,
				change.GetCount(QUARTER),QUARTER);
			mError=buffer;
		}
		virtual std::string ToString() const { return mError; }
		std::string mError;
	};

	Change::Change(){
		std::fill_n( myChange, COIN_COUNT, 0 );
	}

	Change::Change(const Change& c){
		//copy c into this
		std::copy(c.myChange, c.myChange+COIN_COUNT, myChange);
	}

	Change::Change(const CFIGHT_Change& c){
		//copy c into this
		std::copy(c.change, c.change+COIN_COUNT, myChange);
	}

	void Change::GetCChange(CFIGHT_Change* result)const{
		//copy this into result
		std::copy(myChange, myChange+COIN_COUNT, result->change);
	}


	//return number of coins of specific value
	int Change::GetCount(Coin c) const {
		return myChange[GetCoinIndex(c)];
	}
	
	//return the current value for one type of coin
	int Change::GetValue(Coin c) const {
		return c * GetCount(c);
	}

	//return the current value for all coins
	int Change::GetTotalValue() const {
		return CoinArray::TotalValue( myChange );
	}

	//return the total number of coins
	int Change::GetTotalCount() const {
		return CoinArray::TotalCount( myChange );
	}

	//return true if change is empty - ie no coins
	bool Change::IsEmpty() const {
		return GetTotalCount()==0;
	}

	//add coins to change
	Change& Change::InsertCoins(Coin c, unsigned int n) {
		myChange[GetCoinIndex(c)]+=n;

		return *this;
	}

	//remove coins from change
	Change& Change::RemoveCoins(Coin c, unsigned int n) {
		if (CoinArray::RemoveCoins( myChange, c, n )!=n)
			throw ChangeException(c,n);

		return *this;
	}

	//add one coin to change
	Change& Change::InsertCoin(Coin c) {
		return InsertCoins(c,1);
	}

	//remove one coin from change
	Change& Change::RemoveCoin(Coin c) {
		return RemoveCoins(c,1);
	}

	//add a specific set of change
	Change& Change::InsertChange(const Change& c){
		CoinArray::InsertCoins( myChange,c.myChange );
		return *this;
	}

	//extract a specific set of change
	Change& Change::RemoveChange(const Change& c){
		if (CoinArray::RemoveCoins( myChange, c.myChange  )!=c.GetTotalCount())
			throw ChangeException(c);

		return *this;
	}

	//
	// class Move
	//

	Move::Move(Coin give, const Change& take)
		: myGive(give), myTake(take)
	{
		if (take.GetTotalValue()>=give)
			throw Illegal();
	}

	Move::Move(Coin give)
		: myGive(give)
	{ }

	Move::Move(const CFIGHT_Move& cmove)
		: myGive(cmove.play_coin), myTake( cmove.take_change )
	{
		if (myTake.GetTotalValue()>=myGive)
			throw Illegal();
	}

	//
	// class Player
	//

	int Player::theNextUID = 1;

	Player::Player( const std::string& title, const std::string& author ) :
		myUID(theNextUID++),
		myTitle(title),
		myAuthor(author)
	{
		PlayerRegister::Instance().RegisterPlayer( this );
	}
	
	//virtual 
	void Player::NotifyGameStart(const Game&)
	{
		//support legacy behaviour, fwd notification
		NotifyGameStart();
	}

	//virtual 
	void Player::NotifyWon()
	{
	}

	//virtual 
	void Player::NotifyEliminated()
	{
	}

	//virtual
	void Player::NotifyGameStart()
	{
	}

	//virtual 
	Player::~Player()
	{
	}

	//
	//class GameState
	//

	static int games_played = 0;

	GameState::GameState(int player_count) : 
		myTurn(0),
		myPlayersChange( player_count )	  
	{ 		
		//each player gets...
		std::vector< Change >::iterator itr = myPlayersChange.begin();
		std::vector< Change >::iterator end = myPlayersChange.end();
		for(;itr!=end;++itr){
			//default coins
			for (int i=0;i!=COIN_COUNT;++i){
				itr->InsertCoins(COINLIST[i], INITAL_PLAYER_COINS[i]);
			}
		}
	}

	int GameState::GetPlayerCount() const 
	{
		return static_cast<int>(myPlayersChange.size());
	}
	
	int GameState::GetCurrentPlayer() const 
	{
		return myTurn%GetPlayerCount();
	}
	
	int GameState::GetCurrentTurn() const 
	{
		return myTurn;
	}

	//for iterating over players
	int GameState::GetNextPlayer(int currentPlayerIndex) const
	{
		return (++currentPlayerIndex)%GetPlayerCount();
	}
	
	int GameState::GetNextActivePlayer(int currentPlayerIndex) const
	{
		do{
			currentPlayerIndex = GetNextPlayer(currentPlayerIndex);
		}while(!IsPlayerActive(currentPlayerIndex));
		return currentPlayerIndex;
	}

	bool GameState::IsPlayerActive (int player ) const
	{
		if (player<0 || player>=GetPlayerCount())
			throw Exception();

		return !myPlayersChange[player].IsEmpty();
	}

	int GameState::GetActivePlayers()const
	{
		int result = 0;
		for( int i=0;i!=GetPlayerCount();++i){
			if (!myPlayersChange[i].IsEmpty()){
				result++;
			}
		}
		return result;
	}
	
	const Change& GameState::GetPlayerChange( int player ) const
	{
		if (player<0 || player>=GetPlayerCount())
			throw Exception();

		return myPlayersChange[player];
	}

	const Change& GameState::GetGameChange () const
	{
		return myChange;
	}

	GameState GameState::PlayMove( Move move ) const
	{
		//copy game state
		GameState result(*this);

		//perform move transaction
		int whosturn = GetCurrentPlayer();
		result.myPlayersChange[whosturn].RemoveCoin( move.GetCoin() );
		result.myChange.InsertCoin( move.GetCoin() );
		result.myChange.RemoveChange( move.GetChange() );
		result.myPlayersChange[whosturn].InsertChange( move.GetChange() );
		
		//skip to next active player (exit if game over)
		do{
			result.myTurn++;
		}while(!result.IsPlayerActive(result.GetCurrentPlayer()) || result.GetActivePlayers()<1);

		return result;
	}
	
	//
	//class Game
	//

	Game::Game(int player_count) : GameState(player_count)
	{
	}
	
	// create a new game from serialised state
	Game::Game(FILE *f, int player_count, int turn)
		: GameState(player_count)
	{
		myTurn=turn;
		if (Serialise( f, &myChange ))
		{
			for (int i=0;i!=player_count;++i)
			{
				if (Serialise( f, &myPlayersChange[i] )==false)
				{
					throw Exception();
				}
			}
		}
		
		// TOOD: Validate this game state is legitimately achievable
	}
	

	//
	// Tournament Game
	//

	class TournamentGame : public Game {
		
		public:

			//to construct a game pass it a list of players
			TournamentGame (const PlayerList& players);
			TournamentGame (const PlayerList& players, int turn, FILE *f);
			
			//plays out a game, calling out to players for moves and 
			//returning the UID of the winning player
			int PlayGame();

			int GetPlayerUID( int player ) const; 
			clock_t GetPlayerTimeTaken( int player ) const;


		private:
			PlayerList myPlayerList;

			clock_t myCurrentTurnClockStart;
			typedef std::vector<clock_t> PlayerClocks;
			PlayerClocks myPlayerClocks;
	};


	TournamentGame::TournamentGame(const PlayerList& players) : 
		Game( static_cast<int>(players.size()) ),
		myPlayerList( players ),
		myPlayerClocks( players.size(), 0)
	{	}

	TournamentGame::TournamentGame(const PlayerList& players, int turn, FILE *f) :
		Game( f, static_cast<int>(players.size()), turn )
	{	}
	
	struct NotifyGameStartFn
	{
		NotifyGameStartFn( const Game& game )
			: myGame(game)
		{ }

		void operator()(Player* p){
			p->NotifyGameStart(myGame);
		}

		const Game& myGame;
	};

	int TournamentGame::PlayGame()
	{
		games_played++;

		std::for_each(myPlayerList.begin(),myPlayerList.end(),NotifyGameStartFn(*this));
		std::fill(myPlayerClocks.begin(),myPlayerClocks.end(),0);
		
		do{
			const int whosturn = GetCurrentPlayer();
			
			if (!myPlayersChange[whosturn].IsEmpty())
			{
				bool okay=false;	
				try{
					
					if (gVerbose) Serialise(stderr, *this);
					
					TournamentGame firewall(*this);
					
					tms t;
					myCurrentTurnClockStart = times(&t);
					Move move = myPlayerList[whosturn]->GetMove(firewall);					
					myPlayerClocks[whosturn] += (times(&t)-myCurrentTurnClockStart);
					
					if (myPlayerClocks[whosturn]>CFIGHT_PLAYER_TIME_PER_GAME)
						throw OutOfTimeException();

					//if (*this!=firewall)
					//	throw Exception();

					if (gVerbose) Serialise(stderr, move);

					myPlayersChange[whosturn].RemoveCoin( move.GetCoin() );
					myChange.InsertCoin( move.GetCoin() );
					myChange.RemoveChange( move.GetChange() );
					myPlayersChange[whosturn].InsertChange( move.GetChange() );
					
					okay=true;
				}
				catch(Exception& e)
				{
					fprintf(stderr, "Caught Exception: %s\n", e.ToString().c_str());	
				}
				catch(...)
				{
					fprintf(stderr, "Caught unknown Exception running.\n");
				}
				
				if (!okay)
				{
					fprintf(stderr, "Eliminated: %s by %s on turn %i\n", 
						myPlayerList[whosturn]->GetTitle().c_str(),
						myPlayerList[whosturn]->GetAuthor().c_str(),
						GetCurrentTurn());
						
					fprintf(stderr, "Eliminated player had:\n");
					Serialise(stderr, myPlayersChange[whosturn]);
					fprintf(stderr, "after %fs of %fs.\n", 
						(float)myPlayerClocks[whosturn]/ticks_per_s,
						(float)CFIGHT_PLAYER_TIME_PER_GAME/ticks_per_s);
					
					//eliminate "cheaters" by taking all their money
					myPlayersChange[whosturn].RemoveChange( myPlayersChange[whosturn] );
				}

				if (myPlayersChange[whosturn].IsEmpty())
					myPlayerList[whosturn]->NotifyEliminated();

			}
			myTurn++;
		}while(GetActivePlayers() > 1);

		//one active player left, find who...
		const int result = GetNextActivePlayer(GetCurrentPlayer());
		myPlayerList[result]->NotifyWon();
		return myPlayerList[result]->GetUID();
	}

	//virtual
	int TournamentGame::GetPlayerUID( int player ) const
	{
		if (player<0 || player>=GetPlayerCount())
			throw Exception();

		return myPlayerList[player]->GetUID();
	}

	//virtual
	clock_t TournamentGame::GetPlayerTimeTaken( int player ) const	
	{
		if (player<0 || player>=GetPlayerCount())
			throw Exception();

		tms t;
		return myPlayerClocks[player] + (times(&t)-myCurrentTurnClockStart);
	}

	// 
	// non-member serialise functions
	// 
	
	// Write Change
	void Serialise(FILE* f, const Change& change)
	{
		fprintf(f, "%ix%i, %ix%i, %ix%i, %ix%i\n", 
			change.GetCount(PENNY),PENNY,
			change.GetCount(NICKEL),NICKEL,
			change.GetCount(DIME),DIME,
			change.GetCount(QUARTER),QUARTER);
	}

	std::string ReadLine(FILE* f)
	{
		std::string r = "";
		const int buffsize = 16;
		char buffer[buffsize];
		while (fgets(buffer, buffsize, f))
		{
			r.append(buffer);
			if (r[r.size()-1]=='\n')
			{
				r.pop_back();
				break;
			}
		}
		return r;
	}
	
	// Read Change
	bool Serialise(FILE* f, Change* change)
	{
		// empty
		Change result;
		
		std::string line = ReadLine(f);
		std::vector<std::string> pairs;
		Split(line, ',', &pairs);
		for(int i=0;i!=pairs.size();++i)
		{
			std::vector<std::string> pair;
			
			// expect a pair separated by an 'x'
			Split(pairs[i], 'x', &pair);
			if (pair.size()!=2) return false;
			
			// convert to integers
			int c = atoi(pair[0].c_str());
			int d = atoi(pair[1].c_str());

			// check its a valid face value
			if (std::find(COINLIST, COINLIST+COIN_COUNT, d)==COINLIST+COIN_COUNT)
				return false;
			
			// add to the pool
			result.InsertCoins((Coin)d,c);
		}
		
		return true;
	}
		
	// Write Game
	void Serialise(FILE* f, const GameState& gameState)
	{
		fprintf(f, "%i %i\n", gameState.GetPlayerCount(), gameState.GetCurrentTurn() );
		Serialise( f, gameState.GetGameChange() );
		for (int i=0;i!=gameState.GetPlayerCount();++i)
		{
			Serialise( f, gameState.GetPlayerChange(i) );
		}
	}
	
	bool Serialise(FILE* f, const PlayerList& all_players, TournamentGame** pGame)
	{
		bool success=false;
		
		*pGame=0;
		int player_c, turn;
		if (fscanf(f, "%d %d\n", &player_c, &turn )==2)
		{
			if (player_c<=all_players.size())
			{
				// create subset
				PlayerList players(all_players);
				players.resize(player_c);
				
				*pGame=new TournamentGame(players,turn,f);
				success=true;
			}
			else
			{
				fprintf(stderr, "Too many players, read %i, maximum %i\n", 
					player_c, static_cast<int>(all_players.size()));
			}
		}
		
		return success;
	}
	
	// Write Move
	void Serialise(FILE* f, const Move& move)
	{
		fprintf(f, "%i\n",move.GetCoin());
		Serialise( f, move.GetChange() );
	}
	
	// Read Move
	bool Serialise(FILE* f, Move* pMove)
	{
		bool result=false;

		int coin;
		if (fscanf(f, "%d\n",&coin)==1)
		{
			// TODO: Validate "coin" is a valid Coin value
			Coin give=(Coin)coin;
			Change take;
			if (Serialise(f, &take))
			{
				*pMove = Move(give, take);			
				result=true;
			}
		}
		
		return result;
	}
		
	//
	// Utility functions
	//
	
	// gets the largest value of change possible from a coin -----------------

	Change MaximumChangeValue (const Change& availableChange, 
		                       const Coin coinToChange)
	{
		Change result;

		// work down through coin types

		const int coinIndex = GetCoinIndex(coinToChange);

		if (coinIndex > 0)
		{
			for (int testIndex = coinIndex - 1; testIndex >= 0; --testIndex)
			{
				const Coin testCoin = COINLIST[testIndex];

				const int maxValue = coinToChange - result.GetTotalValue() - 1;
				const int maxCoins = maxValue / testCoin;
				
				const int numCoinsToTake = 
					std::min(maxCoins, availableChange.GetCount(testCoin));

				if (numCoinsToTake > 0)
				{
					result.InsertCoins(testCoin, numCoinsToTake);
				}
			}
		}

		// all done

		return result;
	}

	Change MaximumChangeCount (const Change& availableChange, 
		                       const Coin coinToChange)
	{
		Change result;

		// work down through coin types

		const int coinIndex = GetCoinIndex(coinToChange);

		if (coinIndex > 0)
		{
			for (int testIndex = 0; testIndex <coinIndex; ++testIndex)
			{
				const Coin testCoin = COINLIST[testIndex];

				const int maxValue = coinToChange - result.GetTotalValue() - 1;
				const int maxCoins = maxValue / testCoin;
				
				const int numCoinsToTake = 
					std::min(maxCoins, availableChange.GetCount(testCoin));

				if (numCoinsToTake > 0)
				{
					result.InsertCoins(testCoin, numCoinsToTake);
				}
			}
		}

		// all done

		return result;
	}

	//------------------------------------------------------------------------
	// function:	GetPossibleChange
	//
	// params:		IN		change available to be taken
	//				IN		maximum value of change that may be taken
	//				OUT		all possible change combinations
	//
	// purpose:		finds all possible change combinations from the given 
	//              pool, up to the maximum value specified
	//------------------------------------------------------------------------

	void GetAllPossibleChange (
		const Change& availableChange, 
		const int maximumValue, 
		Change::List& change)
	{
		// calculate the maximum number of different change combinations
		// that could be produced from the available pool

		int coinIndex;   // used to iterate over each coin type
		int maxSize = 1; // records the maximum number of combinations

		for (coinIndex = 0; coinIndex < CPPFight::COIN_COUNT; ++coinIndex)
		{
			maxSize *= 1 + 
				availableChange.GetCount(CPPFight::COINLIST[coinIndex]);
		}

		change.reserve(maxSize);

		// begin by clearing and adding the null change set
		change.resize(0);
		change.resize(1);

		int totalCount = 1; // tracks the actual number of valid combinations

		// iterate through available change, adding coins of each type to all
		// of the existing combinations within the maximum value constraints

		for (coinIndex = 0; coinIndex < CPPFight::COIN_COUNT; ++coinIndex)
		{
			const CPPFight::Coin& coin = CPPFight::COINLIST[coinIndex];

			const int availableCoins = availableChange.GetCount(coin);

			if (availableCoins > 0)
			{
				int currentCount = 0; // track number of new combinations 
					// using the current coin
				
				// try adding coins to each previous change combinations

				for (int oldIndex = 0; oldIndex < totalCount; ++oldIndex)
				{
					const CPPFight::Change& oldChange = change[oldIndex];

					int value = oldChange.GetTotalValue() + coin;
						// tracks the value of the new combination

					int coinsToAdd = 1; // tracks the number of coins of 
						// current type to add to old combination

					// only create change combinations if their value does not
					// exceed the maximum value allowed

					while ((value <= maximumValue) && (coinsToAdd <= availableCoins))
					{
						change.push_back(oldChange);
						change.back().InsertCoins(coin, coinsToAdd);

						++currentCount;
						++coinsToAdd;
						value += coin;
					}
				}

				totalCount += currentCount;
			}
		}

		// notes: this code is designed to work for any combination of coin
		// types. in practice, the largest 'maximumValue' received will be 24, 
		// and the QUARTER coins will never be used. this leaves considerable
		// room for optimisation if necessary.
	}
}

//
// Implementation of C linkage function interface
//

int CFIGHT_Coin2Index( CFIGHT_Coin coinType ){	
	return CPPFight::GetCoinIndex( coinType );	
}

CFIGHT_Coin CFIGHT_Index2Coin( int i ){
	return CPPFight::COINLIST[i];
}

int CPPFight_GetInitialPlayerCoins( int i ){
	return CPPFight::INITAL_PLAYER_COINS[i];
}

int CFIGHT_Change_GetTotalValue( const CFIGHT_Change* change ){
	return CPPFight::CoinArray::TotalValue(change->change);
}

int CFIGHT_Change_GetTotalCount( const CFIGHT_Change* change ){
	return CPPFight::CoinArray::TotalCount(change->change);
}

//add coins to change
void CFIGHT_Change_InsertCoins( CFIGHT_Change* change, CFIGHT_Coin c, unsigned int n ){
	change->change[CPPFight::GetCoinIndex(c)]+=n;
}

//remove coins to change
int CFIGHT_Change_RemoveCoins( CFIGHT_Change* change, CFIGHT_Coin c, unsigned int n ){
	return CPPFight::CoinArray::RemoveCoins( change->change, c, n );
}

/* add coins to change */
void CFIGHT_Change_InsertChange( CFIGHT_Change* change, const CFIGHT_Change* other ){
	CPPFight::CoinArray::InsertCoins( change->change, other->change );
}

/* remove coins from change (returns number of coins removed, does not remove partial amounts) */
int CFIGHT_Change_RemoveChange( CFIGHT_Change* change, const CFIGHT_Change* other ){
	return CPPFight::CoinArray::RemoveCoins( change->change, other->change );
}

// get the largest value of change possible from a coin 
void CFIGHT_MaximumChangeValue (const CFIGHT_Change* availableChange, CFIGHT_Coin coinToChange, CFIGHT_Change* result){
	CPPFight::MaximumChangeValue(CPPFight::Change(*availableChange), coinToChange).GetCChange(result);
}

// gets the largest number of coins in change possible from a coin
void CPPFight_MaximumChangeCount (const CFIGHT_Change* availableChange, CFIGHT_Coin coinToChange, CFIGHT_Change* result){
	CPPFight::MaximumChangeCount(CPPFight::Change(*availableChange), coinToChange).GetCChange(result);
}

//
// CPlayer
// implements callbacks for C players
//
namespace CPPFight{
	class CPlayer : public Player 
	{
		public:
			CPlayer( const char * title, const char * author, const CPPFight_CPlayerCfg& cfg )
				: Player( title, author ),
				  myCPlayer_cfg(cfg)
			{	}

			// the main hoo-har, examine the game, and return your move...
			virtual Move GetMove( const Game& theGame ){				
				return Move( 
					myCPlayer_cfg.pGetMove( (CPPFight_Player)this, (CPPFight_Game)&theGame ) 
				);
			}

			// optional overloads
			// notifies player of game status, start, win/loose
			// useful for learning AI's
			virtual void NotifyGameStart(const Game& theGame){
				if (myCPlayer_cfg.pNotifyGameStart)
					myCPlayer_cfg.pNotifyGameStart( (CPPFight_Player)this, (CPPFight_Game)&theGame );
			}

			virtual void NotifyWon(){
				if (myCPlayer_cfg.pNotifyWon)
					myCPlayer_cfg.pNotifyWon( (CPPFight_Player)this );
			}

			virtual void NotifyEliminated(){
				if (myCPlayer_cfg.pNotifyEliminated)
					myCPlayer_cfg.pNotifyEliminated( (CPPFight_Player)this );
			}

			void* GetCustomData()const{
				return myCPlayer_cfg.pCustomData;
			}
			
			~CPlayer(){
				if (myCPlayer_cfg.pFreeCustomData)
					myCPlayer_cfg.pFreeCustomData(GetCustomData());
			}
			
		private:
			CPPFight_CPlayerCfg myCPlayer_cfg;
	};
}//namespace CPPFight

void CPPFight_New_Player(const char * title, const char * author, CPPFight_CPlayerCfg cfg){
	
	//uses a simple hidden singleton for memory management
	static class CPlayerManager{
		public:
			CPPFight::CPlayer* NewPlayer(const char * title, const char * author, CPPFight_CPlayerCfg cfg){
				CPPFight::CPlayer* ptr = new CPPFight::CPlayer( title, author, cfg );
				myPlayerList.push_back(ptr);
				return ptr;
			}

			~CPlayerManager(){
				std::for_each(myPlayerList.begin(), myPlayerList.end(), CPlayerManager::Destroy);
			}

		private:
			std::vector<CPPFight::CPlayer*> myPlayerList;

			static void Destroy(CPPFight::CPlayer* ptr){
				delete ptr;
			}
	} cPlayerManager;

	cPlayerManager.NewPlayer ( title, author, cfg );
}

int CPPFight_Player_GetUID(CPPFight_Player the){
	return static_cast<const CPPFight::CPlayer*>(the)->GetUID();
}

void* CPPFight_Player_GetCustomData(CPPFight_Player the){
	return static_cast<const CPPFight::CPlayer*>(the)->GetCustomData();
}

// GameState functions

// how many players in the game
int CPPFight_GameState_GetPlayerCount(CPPFight_GameState the){
	return static_cast<const CPPFight::GameState*>(the)->GetPlayerCount();
}

// the current player index (ie you, if calling from inside CPPFight_Player_GetMove callback)
int CPPFight_GameState_GetCurrentPlayer(CPPFight_GameState the){
	return static_cast<const CPPFight::GameState*>(the)->GetCurrentPlayer();
}
		
// for iterating over players
int CPPFight_GameState_GetNextPlayer(CPPFight_GameState the, int currentPlayerIndex){
	return static_cast<const CPPFight::GameState*>(the)->GetNextPlayer(currentPlayerIndex);
}

int CPPFight_GameState_GetNextActivePlayer(CPPFight_GameState the, int currentPlayerIndex){
	return static_cast<const CPPFight::GameState*>(the)->GetNextActivePlayer(currentPlayerIndex);
}

// the number of players *still active* in game
int CPPFight_GameState_GetActivePlayers(CPPFight_GameState the){
	return static_cast<const CPPFight::GameState*>(the)->GetActivePlayers();
}

// 1 if a player (by index) has any change left, 0 otherwise 
int CPPFight_GameState_IsPlayerActive(CPPFight_GameState the, int player ){
	return static_cast<const CPPFight::GameState*>(the)->IsPlayerActive(player)?1:0;
}

// change pool for a specific player (by index)
void CPPFight_GameState_GetPlayerChange(CPPFight_GameState the, int player, CFIGHT_Change* result ){
	static_cast<const CPPFight::GameState*>(the)->GetPlayerChange(player).GetCChange(result);
}
		
// change pool currently available in the game (on the table)
void CPPFight_GameState_GetGameChange(CPPFight_GameState the, CFIGHT_Change* result ){
	static_cast<const CPPFight::GameState*>(the)->GetGameChange().GetCChange(result);
}

// returns a new GameState object, representing the state of the game once a move has been played
CPPFight_GameState CPPFight_GameState_PlayMove( CPPFight_GameState the, const CFIGHT_Move* move ){
	return new CPPFight::GameState(static_cast<const CPPFight::GameState*>(the)->PlayMove(CPPFight::Move(*move)) );
}

// cleans up memory used in GameState objects returned by above function call
void CPPFight_Delete_GameState( CPPFight_GameState the ){
	delete static_cast<CPPFight::GameState*>(the);
}

// Game functions

int CPPFight_Game_GetPlayerUID( CPPFight_Game the, int player ){
	return static_cast<const CPPFight::Game*>(the)->GetPlayerUID(player);
}

clock_t CPPFight_Game_GetPlayerTimeTaken( CPPFight_Game the, int player ){
	return static_cast<const CPPFight::Game*>(the)->GetPlayerTimeTaken(player);
}

CPPFight_GameState CPPFight_Game_GetGameState( CPPFight_Game the ){
	CPPFight::Game* game = static_cast<CPPFight::Game*>(the);
	CPPFight::GameState* gameState = static_cast<CPPFight::GameState*>(game);
	return gameState;
}

//
// main application / tournament
//

extern "C" { void CFIGHT_CreateAllPlayers(); }

void Split(const std::string& in, char c, std::vector<std::string>* pOut)
{
	std::string::const_iterator i = in.begin();
	while(i!=in.end()){
		std::string::const_iterator j = std::find(i,in.end(),c);
		pOut->push_back(std::string(i,j));
		if (j!=in.end()) ++j;
		i=j;
	}	
}

void AddCLPlayer(
	const std::string& commandline, 
	const std::string& ai_name, 
	const std::string& author)
{
	if (commandline.empty()==false)
	{
		std::vector<std::string> args;
		Split(commandline, ' ', &args );
	
		// self registers instance, permanently available
		// (doesn't 'leak', cleaned up by OS on app exit!)
		new POpenPlayer(args, ai_name, author);
	}
}

int ParseCLInt(const char* str, int lower, int upper, const char* docstr)
{
	int result=atoi(str);
	if (result<lower || result>upper) {
		fprintf(stderr,"Illegal %s: %s (%i)\n", 
			docstr,
			str,
			result);
		exit(-1);
	}
	return result;
}

void AddCLPlayers(int argc, char* argv[], std::vector<std::string>* pMoves )
{	
	std::string command, ai_name, author;
	for (int i=0;i<argc;++i)
	{
		if (*argv[i]=='-')
		{
			if (!command.empty())
			{
				AddCLPlayer( command, ai_name, author );
				command="";
				ai_name="";
				author="";
			}
			
			// add New (externally implemented) bot
			int ii=i;
			char mode = *(argv[i]+1);
			if (mode) 
			{
				if (*(argv[i]+2)!=0) {
					command = (argv[i]+2);
				}
				else
				{
					++i;
					if (i!=argc & argv[i]!=0) command = argv[i];
				}
			}
			
			// N = New AI (continued)
			switch (mode){
				// Moves mode: read a game state and out put the move selected 
				// by the requested bot
				case 'm':
					pMoves->push_back(command);
					command="";
					break;
				case 't':
					gTspec=command;
					command="";
					break;
				case 'o':
					gOspec=command;
					command="";
					break;
				// add a New bot
				case 'n':
					break;
				// eXclude a bot
				case 'x':
					CPPFight::PlayerRegister::Instance().Exclude(command);
					command="";
					break;
				// be Verbose
				case 'v':
					gVerbose=true;
					command="";
					break;
				// set Games per match (1v1)
				case 'g':
					gGamesPerMatch=ParseCLInt(command.c_str(),1,65536,"games per match");
					command="";
					break;
				case 's':
					gSecondsPerGame=ParseCLInt(command.c_str(),1,60*60,"seconds per game");
					command="";
					break;
				default:
					fprintf(stderr,"Unrecognised command %s\n", argv[ii]);
					exit(-1);
			}
		}
		else if (!command.empty() && ai_name.empty())
		{
			ai_name = argv[i];
		}
		else if (!command.empty() && !ai_name.empty() && author.empty())
		{
			author = argv[i];
		}
		else if (command.empty())
		{
			// unexpected
		}
	}

	AddCLPlayer(command, ai_name, author);
}

// comparison functor for sorting players based on score
class CompScores
{
	public:
		CompScores(std::map< int, int >& scores)
			: myScores(scores)
		{
		}
		bool operator()(CPPFight::Player* a, CPPFight::Player* b)
		{
			return myScores[a->GetUID()] > myScores[b->GetUID()];
		}
	private:
		std::map< int, int >& myScores;
};

class TournamentResultsFormatter
{
	public:
		virtual void BeginTournament(const CPPFight::PlayerList& t) = 0;
		virtual void BeginRound(const CPPFight::PlayerList& r, int matches) = 0;
		virtual void AddMatchResult(
			const CPPFight::PlayerList& match,
			const std::vector<int>& games 
		)=0;
		virtual void EndRound() = 0;
		virtual void EndTournament()=0;
};

class JsonResultsFormatter : public TournamentResultsFormatter
{
	public:
		JsonResultsFormatter()
		{
			m_tournament=0;
		}
		~JsonResultsFormatter()
		{
			if (m_tournament>0) printf("]\n");
		}
		virtual void BeginTournament(const CPPFight::PlayerList& t)
		{
			if (m_tournament>0) printf(",");
			else printf("[\n");
			printf("\{\n");
			printf("\"players\":[\n");
			for(int i=0;i!=t.size();++i)
			{
				if (i>0)  printf(",");
				printf("{\"uid\":%i,\"title\":\"%s\",\"author\":\"%s\"}",
					t[i]->GetUID(),
					t[i]->GetTitle().c_str(),
					t[i]->GetAuthor().c_str());
			}
			printf("],");
			printf("\"rounds\":[\n");
			m_round=0;
		}
		virtual void BeginRound(const CPPFight::PlayerList& r, int matches){
			if (m_round>0) printf(",");
			printf("[");
			m_match=0;
		}
		virtual void AddMatchResult(
			const CPPFight::PlayerList& match,
			const std::vector<int>& games 
		)
		{
			if (m_match>0) printf(",");
			printf("[");
			for(int i=0;i!=match.size();++i)
			{
				if (i>0) printf(",");
				printf("{\"%i\":%i}", match[i]->GetUID(), games[i]);
			}
			printf("]");
			m_match++;
		}
		virtual void EndRound()
		{
			m_round++;
			printf("]");
		}
		virtual void EndTournament()
		{
			printf("]}\n");
			m_tournament++;
		}
		int m_tournament;
		int m_round;
		int m_match;
};

class PrintfRoundRobinResultsFormatter : public TournamentResultsFormatter
{
	public:
		PrintfRoundRobinResultsFormatter()
		{
			fi=0;
			fj=0;
		}
		
		virtual void BeginTournament(const CPPFight::PlayerList& _tournament) 
		{
			tournament = _tournament;
			xTable.resize( tournament.size() );
			
			printf(" Key:\n");
			for(int i=0;i<tournament.size();++i){
			
				xTable[i].resize( tournament.size() );
				for(int j=0;j<tournament.size();++j)
					xTable[i][j]=-1;
				
				printf("  %2i)\t%s by %s\n", 
					tournament[i]->GetUID(),
					tournament[i]->GetTitle().c_str(),
					tournament[i]->GetAuthor().c_str());
			}

			printf(" Cross Table (rows=player 1, columns=player 2, table shows player 1 wins):\n\t");
			CPPFight::PlayerList::const_iterator k;
			for(k=tournament.begin();k!=tournament.end();++k){
				printf("%2i\t", (*k)->GetUID() );
			}
			printf("\n\t");	
			for(k=tournament.begin();k!=tournament.end();++k){
				printf("--\t" );
			}
			printf("\n");
		}
		
		virtual void AddMatchResult(
			const CPPFight::PlayerList& match,
			const std::vector<int>& games 
		)
		{
			assert( games.size()==2 );
			
			const int i0=IndexOf(match[0]->GetUID());
			const int i1=IndexOf(match[1]->GetUID());
			FlushXTable(i0,i1,games[0]);
			
			rr_points_scores[ match[0]->GetUID() ] += games[0];
			rr_points_scores[ match[1]->GetUID() ] += games[1];
						
			//transfer match score to win/lose/draw score
			if (games[0] > games[1]){
				rr_wld_scores[match[0]->GetUID()]++;
			}
			else if(games[0] < games[1]){
				rr_wld_scores[match[1]->GetUID()]++;
			}
		}
		
		virtual void EndTournament()
		{
			//sort the player list on points score
			CPPFight::PlayerList tournament_sorted(tournament);
			std::sort( tournament_sorted.begin(), tournament_sorted.end(), CompScores(rr_points_scores) );
	
			printf(" Totals:\n  Games:\n");

			for(int i=0;i<tournament_sorted.size();++i){
				printf("%6i - %s by %s\n", 
					rr_points_scores[ tournament_sorted[i]->GetUID() ],
					tournament_sorted[i]->GetTitle().c_str(),
					tournament_sorted[i]->GetAuthor().c_str());
			}

			//sort the player list on score
			std::sort( tournament_sorted.begin(), tournament_sorted.end(), CompScores(rr_wld_scores) );
			printf("  Matches:\n");

			for(int i=0;i<tournament_sorted.size();++i){
				printf("%6i - %s by %s\n", 
					rr_wld_scores[ tournament_sorted[i]->GetUID() ],
					tournament_sorted[i]->GetTitle().c_str(),
					tournament_sorted[i]->GetAuthor().c_str());
			}
		}

		//unused in this round-robin formatter
		virtual void BeginRound(const CPPFight::PlayerList& r, int matches){}
		virtual void EndRound(){}
		
	private:
		CPPFight::PlayerList tournament;
		std::vector< std::vector<int> > xTable;
		std::map< int, int > rr_points_scores;	//total games won count
		std::map< int, int > rr_wld_scores;		//matches (N games) won/lost/draw
		
		int IndexOf( int id ) {
			for (int i=0;i!=tournament.size();++i)
				if (tournament[i]->GetUID()==id) return i;
			return -1;
		}
		
		int fi,fj;
		void FlushXTable(int i,int j,int s)
		{
			xTable[i][j] = s;
			
			while(xTable[fi][fj]!=-1 || fi==fj)
			{
				if (fj==0) printf("  %2i)\t", tournament[fi]->GetUID() );
				if (fi==fj) printf( "--\t" );
				else printf("%3i\t", xTable[fi][fj] );
				
				fj++;
				if (fj>=tournament.size())
				{
					fj=0;
					fi++;
					printf("\n");
					if (fi>=tournament.size())
						break;
				}
			}
		}
};

class PrintfEliminationFormatter : public TournamentResultsFormatter
{
	public:
		virtual void BeginTournament(const CPPFight::PlayerList& t) 
		{
			printf("\nMulitiplayer Elimination Tournament\n");
			round=1;
		}
		virtual void BeginRound(const CPPFight::PlayerList& r, int matches) 
		{
			printf(" Round %i - %i players, %i groups\n",
				round,
				static_cast<int>(r.size()),
				matches);
			group=1;
		}
		virtual void AddMatchResult(
			const CPPFight::PlayerList& match,
			const std::vector<int>& games 
		)
		{
			printf("  Group %i, %i players.  Scores:\n", 
				group, 
				static_cast<int>(match.size()) );

			for(unsigned int i=0;i<match.size();++i){
				printf("    %i - %s by %s\n", 
					games[ i ],
					match[i]->GetTitle().c_str(),
					match[i]->GetAuthor().c_str());
			}
			
			group++;		
		}
		virtual void EndRound()
		{
			round++;
		}
		virtual void EndTournament(){}
		
	private:
		int round;
		int group;
};

int RoundRobin(const CPPFight::PlayerList& tournament, TournamentResultsFormatter* pOut)
{
	int errors = 0;
	
	//Round robin
	pOut->BeginTournament(tournament);
	
	// all in 1 big round -- though i guess this could be split into 2 rounds
	// All the AvsB then all the BvsA, or something like that, not sure theres any point
	pOut->BeginRound(tournament, (tournament.size()-1)*tournament.size());
	
	for(CPPFight::PlayerList::const_iterator j=tournament.begin();j!=tournament.end();++j){
		for(CPPFight::PlayerList::const_iterator k=tournament.begin();k!=tournament.end();++k){
			if (j!=k){

				CPPFight::PlayerList players(2);
				players[0] = *j;
				players[1] = *k;

				std::map< int, int > match_score;
				for(int i=0;i<gGamesPerMatch;i++){
					CPPFight::TournamentGame theGame( players );
					int winner = theGame.PlayGame();
					if (winner!=-1){
						match_score[ winner ]++;
					}
					else{
						errors++;
					}
				}

				std::vector<int> scores(2);
				scores[0] = match_score[ players[0]->GetUID() ];
				scores[1] = match_score[ players[1]->GetUID() ];
				pOut->AddMatchResult(players, scores);

			}
		}
	}

	pOut->EndRound();
	pOut->EndTournament();
	
	return errors;
}

int Elimination(const CPPFight::PlayerList& tournament_, TournamentResultsFormatter* pOut)
{
	int errors = 0;
	
	// working copy
	CPPFight::PlayerList tournament = tournament_;
	
	pOut->BeginTournament(tournament);

	while(tournament.size()>1){
		std::map< int, int > score_map;
		std::vector< CPPFight::PlayerList > groups(tournament.size()/6+1);
		
		pOut->BeginRound(tournament, groups.size());

		for(unsigned int i=0;i!=tournament.size();++i){
			groups[ i%groups.size() ].push_back( tournament[i] );
		}

		CPPFight::PlayerList winners;

		for(unsigned int g = 0;g!=groups.size();++g){
		
			CPPFight::PlayerList playerList(groups[g]);
			std::sort( playerList.begin(), playerList.end() );
			do{
				CPPFight::TournamentGame theGame( playerList );
				int winner = theGame.PlayGame();
				if (winner!=-1){
					++score_map[ winner ];
				}
				else{
					++errors;
				}
			}while(std::next_permutation(playerList.begin(), playerList.end()));

			std::sort( groups[g].begin(), groups[g].end(),	CompScores(score_map) );
			std::vector<int> scores(groups[g].size());
			for(int i=0;i!=groups[g].size();++i)
				scores[i] = score_map[ groups[g][i]->GetUID() ];

			pOut->AddMatchResult(groups[g], scores);
		
			// players going to next round -> top half of table
			CPPFight::PlayerList::iterator begin = groups[g].begin();
			CPPFight::PlayerList::iterator end = begin+(groups[g].size()+1)/2;
		
			// let extra contestants into next round in case of ties in table center
			while( end!=groups[g].end() && scores[(*(end-1))->GetUID()]==scores[(*end)->GetUID()] ){
				++end;
			}

			// where including ties means all players pass to next round, and it's already 
			// down to one group, exclude ties from winners circle
			if ( end==groups[g].end() && groups.size()==1){               
				do{
					--end;
				}while( std::distance(groups[g].begin(),end)>0 && 
				        scores[(*(end-1))->GetUID()]==scores[(*end)->GetUID()] );
			}
		
			winners.insert(winners.end(), begin, end);
		}

		tournament = winners;
		pOut->EndRound();
	}
	
	pOut->EndTournament();
	
	return errors;
}

int main(int argc, char* argv[])
{
	CFIGHT_CreateAllPlayers();

	std::vector<std::string> moves;
	AddCLPlayers(argc, argv, &moves);
	
	if (!moves.empty()) {
		
		CPPFight::PlayerList players = 
			CPPFight::PlayerRegister::Instance().GetPlayerList();
	
		int i;
		for (i=0;i!=players.size();++i){
			if (players[i]->GetTitle()==moves[0]){
				if (gVerbose) 
				{
					fprintf(stderr,"AI Selected: %s by %s\n", 
						players[i]->GetTitle().c_str(),
						players[i]->GetAuthor().c_str());
				}
				break;
			}
		}
		
		if (i==players.size())
		{
			fprintf(stderr,"'%s' NOT FOUND\n", moves[0].c_str());
		}
		else {
			CPPFight::TournamentGame* pGame=0;
			if (CPPFight::Serialise(stdin, players, &pGame))
			{
				try{
					CPPFight::Move move = players[i]->GetMove(*pGame);
					CPPFight::Serialise(stdout, move);			
				}
				catch(...){
					fprintf(stderr,"%s threw an exception, ending\n", 
						players[i]->GetTitle().c_str());
					return -1;
				}
				// delete the TournamentGame created by Serialise
				delete pGame;
				pGame=0;

			}
		}
		return 0;
	}
	
	unsigned int errors = 0;
	
	tms t;
	clock_t begin = times(&t);

	if (gVerbose) printf("C++ FIGHT (c) T.Frogley 2001,2002,2013\n");

	CPPFight::PlayerList tournament=CPPFight::PlayerRegister::Instance().GetPlayerList();

	PrintfEliminationFormatter pfErf;
	PrintfRoundRobinResultsFormatter pfRRrf;
	JsonResultsFormatter jRF;
	
	TournamentResultsFormatter *pRRRF=0, *pERF=0;
	if (gOspec[0]=='t') 
	{
		pRRRF=&pfRRrf;
		pERF=&pfErf;
	}
	else if (gOspec[0]=='j') 
	{
		pRRRF=&jRF;
		pERF=&jRF;
	}
	else
	{
		fprintf(stderr,"Unrecognised Output Format: %s\n", gOspec.c_str() );
		exit(-1);
	}

	for (std::string::iterator i=gTspec.begin(); i!=gTspec.end(); ++i)
	{
		switch (*i)
		{
			case 'r':
			{
				errors += RoundRobin(tournament, pRRRF);
				break;
			}
			case 'e':
			{
				errors += Elimination(tournament, pERF);
				break;
			}
			default:
				fprintf( stderr,
					"Unrecognised tournament type %c in %s\n", 
					*i, gTspec.c_str()
				);
				break;
		}
	}
		
	if (gVerbose)
	{
		printf("Played %i games in %f seconds.  (%i game errors)\n", 
			CPPFight::games_played, 
			(float)(times(&t)-begin)/ticks_per_s, 
			errors);
	}
	return 0;
}