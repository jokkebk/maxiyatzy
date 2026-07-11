# Finnish Tactic Maxi-Yatzy rules used by the solver

This document fixes the exact solitaire rules whose optimal expected-score
policy the lookup table represents.

## Turn structure

- The scorecard has twenty categories and the game has twenty turns.
- A turn begins by rolling all six fair six-sided dice.
- The player may then keep any subset and reroll the rest up to two times.
- The player may stop after any roll. Computationally this is equivalent to
  keeping all six dice through the unused rerolls.
- At the end of a turn exactly one previously unused category is filled.
- **Voluntary crossing is allowed:** any open category may be assigned zero,
  even when another category would score and even when Chance remains open.
- The objective is to maximize expected final score.

## Upper section

| Category | Score |
| --- | --- |
| Ones through Sixes | Sum of dice showing that face |

Reaching at least 75 total upper-section points awards a one-time 50-point
bonus.

## Lower section

Unless stated otherwise, unused dice may be ignored and the highest-scoring
qualifying selection is used.

| Category | Requirement and score |
| --- | --- |
| One pair | Highest pair; sum of its two dice |
| Two pairs | Two distinct face values; sum of the four dice |
| Three pairs | Three distinct face values; sum of all six dice |
| Three of a kind | Highest qualifying face; sum of three dice |
| Four of a kind | Highest qualifying face; sum of four dice |
| Five of a kind | Highest qualifying face; sum of five dice |
| Small straight | `1-2-3-4-5`; 15 points; sixth die ignored |
| Large straight | `2-3-4-5-6`; 20 points; sixth die ignored |
| Full straight | exactly `1-2-3-4-5-6`; 21 points |
| Full house | Three of one face and two of another; sum of those five dice; sixth die ignored |
| Super house | Two distinct triples; sum of all six dice |
| Tower | Four of one face and two of another; sum of all six dice |
| Chance | Sum of all six dice |
| Maxi-Yatzy | Six of one face; 100 points |

Having more matching dice qualifies for an N-of-a-kind selection: for example,
four sixes can supply the three sixes for Three of a Kind. A four-of-a-kind and
a distinct pair can likewise supply a Full House by selecting three of the four
matching dice and ignoring the sixth die.

## Out of scope

- No saved or banked rerolls.
- No Yahtzee bonus or joker rule.
- Other players' scorecards do not affect decisions. The policy maximizes the
  player's own expected score rather than probability of beating a particular
  opponent score.

