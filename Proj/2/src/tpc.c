#include "player.h"
#include "table.h"
#include "keyboard.h"
#include "card.h"
#include "hand.h"
#include "vector.h"

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <semaphore.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <math.h>

/**
 * A couple of global variables grouped in the same struct.
 */
struct
{
    char*        PlayerName; ///< This player name
    char*        ShmName; ///< This shared memory name
    char*        LogFileName; ///< This log file name
    player*      Player; ///< This player pointer
    bool         IsDealer; ///< True if this player is dealer
    table*       Table; ///< Table pointer
    unsigned int NumberOfPlayers; ///< Number of players
    unsigned int NumberOfRounds; ///< Number of rounds
    keyboard     Keyboard; ///< Keyboard handler
    pthread_t    KeyboardThread; ///< Keyboard thread (keyboard runs in another thread, asynchronously)
    bool         KeyboardThreadExecuting; ///< True if keyboard is active
    int          LogFd; ///< File description of logger
    hand         PlayerHand; ///< This player's cards
    bool         Finished; ///< True if game ended
    int          NameSize; ///< Size of this player's name
} globals;

/**
 * Enumeration of possible actions.
 */
typedef enum action_e
{
    A_DEAL, ///< Deal card
    A_RECEIVE, ///< Receive card
    A_PLAY, ///< Play card
    A_HAND, ///< Show card
} action;

#define ACTION_COUNT A_HAND + 1 ///< Number of actions

/**
 * String representation of actions.
 */
const char* action_str[ACTION_COUNT] = { "deal", "receive_cards", "play", "hand" };

/**
 * Parses console arguments
 * @param  argc Number of arguments
 * @param  argv Array of arguments
 * @return      Returns true if successful, false otherwise
 */
bool ParseArguments(int argc, char** argv);

void NextPlayer(void); ///< Advances turn to next player
void WaitForTurn(void); ///< Blocks until player is in right turn
void WaitForGameStart(void); ///< Blocks until game starts

void Play(void); ///< Blocks and plays a card
void* DealCards(void*); ///< Distribute cards
void ReceiveCards(void); ///< Gets cards from fifo

void LogHeader(void); ///< Writes header to log file
void Log(action a); ///< Writes action to log file

void* KeyboardFunc(void*); ///< Function to start keyboard thread

/**
 * Prints information on how to use this program
 * @param err if true, info will be printed to stderr; otherwise stdout
 */
void PrintUsage(bool err);

void JoinTable(void); ///< Join this player to table

void InitGlobals(void); ///< Initialize members of global struct

void ExitHandler(void); ///< atexit handler

/**
 * Entry point for this program
 * @param  argc Number of arguments
 * @param  argv Array of arguments
 * @return      0 if successful, != 0 otherwise
 */
int main(int argc, char** argv)
{
    srand(time(NULL));

    InitGlobals();

    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
    {
        PrintUsage(false);
        return EXIT_SUCCESS;
    }
    else if (!ParseArguments(argc, argv))
    {
        PrintUsage(true);
        return EXIT_FAILURE;
    }

    atexit(ExitHandler);

    JoinTable();

    if (!globals.Table)
        return EXIT_FAILURE;

    WaitForGameStart();

    globals.NumberOfRounds = NUMBER_OF_CARDS / globals.Table->numPlayers;

    globals.NameSize = 8 + table_get_max_player_name_size(globals.Table) + (int)(floor(log10((double)(globals.Table->numMaxPlayers))));

    pthread_t dealThr;

    if (globals.IsDealer)
    {
        LogHeader();
        if (pthread_create(&dealThr, NULL, DealCards, NULL) != 0)
            perror("pthread_create");
    }

    ReceiveCards();

    if (globals.IsDealer)
    {
        pthread_join(dealThr, NULL);
        globals.Table->roundNum++;
    }

    if (pthread_create(&globals.KeyboardThread, NULL, KeyboardFunc, NULL) != 0)
        perror("Keyboard thread creation failed");

    while (globals.Table->roundNum < (int)globals.NumberOfRounds)
    {
        pthread_mutex_lock(&globals.Table->NextPlayerMutex);
        WaitForTurn();

        printf("Round: %d\n", globals.Table->roundNum);

        Play();

        if (globals.IsDealer)
        {
            globals.Table->roundNum++;
        }

        NextPlayer();

        pthread_mutex_unlock(&globals.Table->NextPlayerMutex);
    }

    globals.Finished = true;

    return 0;
}

bool ParseArguments(int argc, char** argv)
{
    if (argc != 4)
        return false;

    /* Player Name */
    globals.PlayerName = strdup(argv[1]);
    /* Shm Name */
    int shmNameSize = strlen(argv[2]) + 1;
    char* tempShmName = malloc(shmNameSize * sizeof(char));
    strcpy(tempShmName, "/");
    strcat(tempShmName, argv[2]);
    globals.ShmName = tempShmName;

    globals.LogFileName = malloc((shmNameSize + 3) * sizeof(char));
    strcpy(globals.LogFileName, argv[2]);
    strcat(globals.LogFileName, ".log");

    printf("%s - %s\n", globals.ShmName, globals.LogFileName);

    int numPlayers = atoi(argv[3]);
    globals.NumberOfPlayers = numPlayers;

    return true;
}

void PrintUsage(bool err)
{
    fprintf(err ? stderr : stdout, "Usage: tpc <player_name> <table_name> <n_players>\n"
            "  player_name  - your name;\n"
            "  table_name - server's name;\n"
            "  n_players - number of players\n");
}

void JoinTable(void)
{
    int shmfd = shm_open(globals.ShmName, O_EXCL | O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);

    if (shmfd < 0) /* Already exists. */
    {
        printf("Table exists... Joining Table...\n");
        shmfd = shm_open(globals.ShmName, O_RDWR , 0775);
        globals.Table = mmap(0, sizeof(table) + sizeof(player) * globals.NumberOfPlayers, PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);
    }
    else /* Just Created a table with this name */
    {
        printf("Table doesn't exist... Creating Table...\n");

        if (ftruncate(shmfd, sizeof(table) + sizeof(player) * globals.NumberOfPlayers) == -1)
        {
            close(shmfd);
            shm_unlink(globals.ShmName);
            globals.Table = NULL;
            return;
        }
        globals.Table = mmap(0, sizeof(table) + sizeof(player) * globals.NumberOfPlayers, PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);
        *globals.Table = table_new(globals.NumberOfPlayers);
        globals.IsDealer = true;
    }

    int playerNum = globals.Table->numPlayers;

    if (playerNum >= globals.Table->numMaxPlayers)
    {
        printf("Table is full.\n");
        close(shmfd);
        globals.Table = NULL;
        return;
    }

    pthread_mutex_lock(&globals.Table->AccessMutex);

    globals.Table->numPlayers++;

    printf("Your number: \t\t%d\n", playerNum);
    printf("Max number of players: \t%d\n", globals.Table->numMaxPlayers);
    printf("Number of players: \t%d\n", globals.Table->numPlayers);
    printf("Available commands: 'play', 'show-hand', 'show-played-cards'\n");

    globals.Player = &(globals.Table->players[playerNum]);

    globals.Player->number = playerNum;

    player_set_name(globals.Player, globals.PlayerName);

    char tempFifoName[1024];

    sprintf(tempFifoName, "/tmp/%sfifo%d", globals.ShmName, playerNum);

    player_set_fifo_name(globals.Player, tempFifoName);

    pthread_mutex_unlock(&globals.Table->AccessMutex);

    if (globals.IsDealer)
        globals.LogFd = open(globals.LogFileName, O_WRONLY | O_CREAT | O_ASYNC, 0666);
    else
        globals.LogFd = open(globals.LogFileName, O_APPEND | O_WRONLY | O_ASYNC, 0666);

    if (globals.LogFd == -1)
        perror("open Log");

    close(shmfd);
}

void NextPlayer(void)
{
    pthread_mutex_lock(&globals.Table->AccessMutex);
    globals.Table->turn = (globals.Table->turn + 1) % globals.Table->numMaxPlayers;
    pthread_mutex_unlock(&globals.Table->AccessMutex);

    pthread_cond_broadcast(&globals.Table->NextPlayerCondVar);
}

void WaitForTurn(void)
{
    printf("Waiting for turn...\n");

    while (globals.Table->turn != (int)globals.Player->number)
    {
        pthread_cond_wait(&globals.Table->NextPlayerCondVar, &globals.Table->NextPlayerMutex);
        if (globals.Table->nextCard != 0)
        {
            card_s c = card_to_string(&globals.Table->cards[globals.Table->nextCard-1]);
            printf("Player %s played card %s.\n", globals.Table->players[(globals.Table->turn == 0 ? globals.Table->numPlayers - 1 : globals.Table->turn - 1)].name, c.str);
        }
    }
}

void WaitForGameStart(void)
{
    printf("Waiting for game to start...\n");

    pthread_mutex_lock(&globals.Table->StartGameMutex);

    while (globals.Table->numPlayers != globals.Table->numMaxPlayers)
        pthread_cond_wait(&globals.Table->StartGameCondVar, &globals.Table->StartGameMutex);

    pthread_mutex_unlock(&globals.Table->StartGameMutex);

    pthread_cond_broadcast(&globals.Table->StartGameCondVar);
}

void Play(void)
{
    globals.Keyboard.playersTurn = true;
    printf("It's your turn. You can play!\n");
    pthread_mutex_lock(&globals.Keyboard.FinishPlayingMutex);
    pthread_cond_wait(&globals.Keyboard.FinishPlayingCondVar, &globals.Keyboard.FinishPlayingMutex);
    pthread_mutex_unlock(&globals.Keyboard.FinishPlayingMutex);
    globals.Keyboard.playersTurn = false;
    Log(A_PLAY);
    Log(A_HAND); // Debugging
}

void* DealCards(void* dummy __attribute__((unused)))
{
    Log(A_DEAL);
    pthread_mutex_lock(&globals.Table->FifosReadyMutex);
    while (globals.Table->numberFifosReady < globals.Table->numPlayers) pthread_cond_wait(&globals.Table->FifosReadyCondVar, &globals.Table->FifosReadyMutex);

    table_shuffle_cards(globals.Table);

    globals.Table->nextCard = NUMBER_OF_CARDS % globals.Table->numPlayers;

    int* fifosFd = malloc(globals.NumberOfPlayers * sizeof(int));

    for (int i = 0; i < globals.Table->numPlayers; ++i)
    {
        fifosFd[i] = open(globals.Table->players[i].fifoName, O_WRONLY);

        if (fifosFd[i] < 0)
        {
            perror("open fifo deal");
            free(fifosFd);
            exit(EXIT_FAILURE);
        }
    }

    int cardToDeal = globals.Table->nextCard;

    for (unsigned int c = 0; c < globals.NumberOfRounds; ++c)
        for (int i = 0; i < globals.Table->numPlayers; ++i)
            write(fifosFd[i], &globals.Table->cards[cardToDeal++], sizeof(card));


    for (int i = 0; i < globals.Table->numPlayers; ++i)
        close(fifosFd[i]);

    pthread_mutex_unlock(&globals.Table->FifosReadyMutex);

    free(fifosFd);
    return NULL;
}

void ReceiveCards(void)
{
    if (mkfifo(globals.Player->fifoName, 0660) < 0)
    {
        perror("mkfifo");
        exit(EXIT_FAILURE);
    }

    globals.Table->numberFifosReady++;
    pthread_cond_signal(&globals.Table->FifosReadyCondVar);

    int fd = open(globals.Player->fifoName, O_RDONLY);

    if (fd < 0)
    {
        perror("open fifo receive");
        exit(EXIT_FAILURE);
    }

    card tempCard;
    while (read(fd, &tempCard, sizeof(card)) != 0)
        hand_add_card(&globals.PlayerHand, tempCard);

    close(fd);

    unlink(globals.Player->fifoName);

    Log(A_RECEIVE);
}

void* KeyboardFunc(void* dummy __attribute__((unused)))
{
    globals.KeyboardThreadExecuting = true;
    // printf("Keyboard is Playing...\n");
    char buffer[1024];
    while (!globals.Finished)
    {
        printf("Insert your option: ");
        fflush(stdout);
        if (fgets(buffer, 1024, stdin) != NULL)
        {
            if (strncmp(buffer, "play", strlen("play")) == 0)
            {
                if (globals.Keyboard.playersTurn)
                {
                    printf("Your hand: \n");

                    char* handStr = hand_to_string(&globals.PlayerHand);
                    printf("%s\n", handStr);
                    free(handStr);

                    bool success = false;

                    do
                    {
                        printf("Choose a card: ");
                        fflush(stdout);

                        if (fgets(buffer, 1024, stdin) == NULL)
                        {
                            success = false;
                            continue;
                        }

                        int numOfCards = vector_size(&globals.PlayerHand.cards);

                        for (int i = 0; i < numOfCards; ++i)
                        {
                            card* c = vector_get(&globals.PlayerHand.cards, i);
                            card_s cStr = card_to_string(c);
                            if (strncmp(buffer, cStr.str, strlen(cStr.str)) == 0)
                            {
                                globals.Table->cards[globals.Table->nextCard++] = *c;
                                hand_remove_card(&globals.PlayerHand, *c);
                                success = true;
                                break;
                            }
                        }

                        if (!success)
                            printf("Card is not valid. Please try again.\n");

                    }
                    while (!success);


                    pthread_cond_signal(&globals.Keyboard.FinishPlayingCondVar);
                }
                else
                    printf("Not your turn yet...\n");
            }
            else if (strncmp(buffer, "show-hand", strlen("show-hand")) == 0)
            {
                char* handStr = hand_to_string(&globals.PlayerHand);
                printf("%s\n", handStr);
                free(handStr);
                Log(A_HAND);
            }
            else if (strncmp(buffer, "show-played-cards", strlen("show-played-cards")) == 0)
            {
                if (globals.Table->nextCard > 0)
                {
                    card_s cardStr = card_to_string(&globals.Table->cards[0]);
                    printf("%s", cardStr.str);
                    for (int i = 1; i < globals.Table->nextCard; ++i)
                    {
                        cardStr = card_to_string(&globals.Table->cards[i]);
                        printf(", %s", cardStr.str);
                    }
                    printf("\n");
                }
                else
                    printf("There are no played cards yet...\n");
            }
            else
            {
                printf("Unrecognized action...\n");
            }

        }
    }

    // printf("Keyboard is Finished...\n");
    globals.KeyboardThreadExecuting = false;
    return NULL;
}

void LogHeader(void)
{
    char buffer[1024];
    //strcpy(buffer, "");
    sprintf(buffer, "%-19s | %*s | %-13s | result\n", "when", -globals.NameSize, "who", "what");
    pthread_mutex_lock(&globals.Table->LoggerMutex);

    lseek(globals.LogFd, 0, SEEK_END);

    write(globals.LogFd, buffer, (strlen(buffer)) * sizeof(char));

    pthread_mutex_unlock(&globals.Table->LoggerMutex);
}

void Log(action a)
{
    char buffer[1024];
    char buffer2[1024];
    strcpy(buffer, "");
    strcpy(buffer2, "");

    time_t now = time(NULL);
    struct tm* timeinfo = localtime(&now);

    strftime(buffer, 1024, "%Y-%m-%d %H:%M:%S", timeinfo);

    char nameBuffer[1024];
    strcpy(nameBuffer, "");

    if (a == A_DEAL)
        sprintf(nameBuffer, "Dealer-%s", globals.Player->name);
    else
        sprintf(nameBuffer, "Player%d-%s", globals.Player->number, globals.Player->name);

    if (a == A_DEAL)
    {
        sprintf(buffer2, "%s | %*s | %-13s | -\n", buffer, -globals.NameSize, nameBuffer, action_str[a]);
    }
    else if (a == A_PLAY)
    {
        card_s cardStr = card_to_string(&globals.Table->cards[globals.Table->nextCard - 1]);
        sprintf(buffer2, "%s | %*s | %-13s | %s\n", buffer, -globals.NameSize, nameBuffer, action_str[a], cardStr.str);
    }
    else if (a == A_RECEIVE || a == A_HAND)
    {
        char* handStr = hand_to_string(&globals.PlayerHand);
        sprintf(buffer2, "%s | %*s | %-13s | %s\n", buffer, -globals.NameSize, nameBuffer, action_str[a], handStr);
        free(handStr);
    }

    pthread_mutex_lock(&globals.Table->LoggerMutex);

    lseek(globals.LogFd, 0, SEEK_END);

    write(globals.LogFd, buffer2, (strlen(buffer2)) * sizeof(char));

    pthread_mutex_unlock(&globals.Table->LoggerMutex);
}

void InitGlobals(void)
{
    globals.PlayerName = NULL;
    globals.ShmName = NULL;
    globals.LogFileName = NULL;
    globals.Player = NULL;
    globals.IsDealer = false;
    globals.Table = NULL;
    globals.NumberOfPlayers = 0;
    globals.NumberOfRounds = 0;
    globals.Keyboard = keyboard_new();
    globals.KeyboardThreadExecuting = false;
    globals.Finished = false;
    globals.PlayerHand = hand_new();
    globals.NameSize = 0;
    globals.LogFd = -1;
}

void ExitHandler(void)
{
    if (globals.IsDealer)
    {
        WaitForTurn();

        munmap(globals.Table, sizeof(table) + sizeof(player) * globals.NumberOfPlayers);
        shm_unlink(globals.ShmName);
    }

    /*if (globals.KeyboardThreadExecuting)
        pthread_join(globals.KeyboardThread, NULL);*/
}
