#ifndef CARD_H
#define CARD_H

/** @defgroup card card
 * @{
 * Structs, arrays and functions used to represent a card.
 */

/**
 * Card suits.
 */
typedef enum suit_e
{
    SUIT_CLUBS, ///< c
    SUIT_SPADES, ///< s
    SUIT_HEARTS, ///< h
    SUIT_DIAMONDS ///< d
} suit;

/**
 * Card ranks.
 */
typedef enum rank_e
{
    RANK_ACE, ///< A
    RANK_TWO, ///< 2
    RANK_THREE, ///< 3
    RANK_FOUR, ///< 4
    RANK_FIVE, ///< 5
    RANK_SIX, ///< 6
    RANK_SEVEN, ///< 7
    RANK_EIGHT, ///< 8
    RANK_NINE, ///< 9
    RANK_TEN, ///< 10
    RANK_JACK, ///< J
    RANK_QUEEN, ///< Q
    RANK_KING ///< K
} rank;

/**
 * String representation of a suit.
 */
static const char* const suit_e_s[] =
{
    "c", "s", "h", "d"
};

/**
 * String representation of a rank.
 */
static const char* const rank_e_s[] =
{
    "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"
};

/**
 * Card.
 */
typedef struct card_t
{
    rank rank; ///< Card's rank.
    suit suit; ///< Card's suit.
} card;

/**
 * String representation of a card.
 */
typedef struct card_s_t
{
    char str[4]; /// Null-terminated string.
} card_s;

/**
 * String representation of a card.
 * @param  card Card.
 * @return A card_s object. Use card_s.str to get a char*.
 */
card_s card_to_string(card* card);

/**@}*/

#endif
