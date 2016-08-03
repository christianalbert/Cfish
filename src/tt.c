/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <string.h>   // For std::memset
#include <stdio.h>

#include "bitboard.h"
#include "tt.h"

TranspositionTable TT; // Our global transposition table


// tt_resize() sets the size of the transposition table, measured in
// megabytes. Transposition table consists of a power of 2 number of
// clusters and each cluster consists of ClusterSize number of TTEntry.

void tt_resize(size_t mbSize)
{
  size_t count = ((size_t)1) << msb((mbSize * 1024 * 1024) / sizeof(Cluster));

  if (count == TT.clusterCount)
    return;

  TT.clusterCount = count;

  free(TT.mem);
  TT.mem = calloc(count * sizeof(Cluster) + CacheLineSize - 1, 1);

  if (!TT.mem) {
    fprintf(stderr, "Failed to allocate %zuMB for transposition table.\n",
            mbSize);
    exit(EXIT_FAILURE);
  }

  TT.table = (Cluster *)((((uintptr_t)TT.mem) + CacheLineSize - 1)
                                               & ~(CacheLineSize - 1));
}


// tt_clear() overwrites the entire transposition table with zeros. It
// is called whenever the table is resized, or when the user asks the
// program to clear the table (from the UCI interface).

void tt_clear(void)
{
  memset(TT.table, 0, TT.clusterCount * sizeof(Cluster));
}


// tt_probe() looks up the current position in the transposition table.
// It returns true and a pointer to the TTEntry if the position is found.
// Otherwise, it returns false and a pointer to an empty or least valuable
// TTEntry to be replaced later. The replace value of an entry is
// calculated as its depth minus 8 times its relative age. TTEntry t1 is
// considered more valuable than TTEntry t2 if its replace value is greater
// than that of t2.

TTEntry *tt_probe(Key key, int *found)
{
  TTEntry *tte = tt_first_entry(key);
  uint16_t key16 = key >> 48; // Use the high 16 bits as key inside the cluster

  for (int i = 0; i < ClusterSize; i++)
    if (!tte[i].key16 || tte[i].key16 == key16) {
      if ((tte[i].genBound8 & 0xFC) != TT.generation8 && tte[i].key16)
        tte[i].genBound8 = (uint8_t)(TT.generation8 | tte_bound(&tte[i])); // Refresh
      *found = (int)tte[i].key16;
      return &tte[i];
    }

  // Find an entry to be replaced according to the replacement strategy
  TTEntry* replace = tte;
  for (int i = 1; i < ClusterSize; i++)
    // Due to our packed storage format for generation and its cyclic
    // nature we add 259 (256 is the modulus plus 3 to keep the lowest
    // two bound bits from affecting the result) to calculate the entry
    // age correctly even after generation8 overflows into the next cycle.
    if (  replace->depth8 - ((259 + TT.generation8 - replace->genBound8) & 0xFC) * 2 * ONE_PLY
        >   tte[i].depth8 - ((259 + TT.generation8 -   tte[i].genBound8) & 0xFC) * 2 * ONE_PLY)
      replace = &tte[i];

  *found = 0;
  return replace;
}


// Returns an approximation of the hashtable occupation during a search. The
// hash is x permill full, as per UCI protocol.

int tt_hashfull(void)
{
  int cnt = 0;
  for (int i = 0; i < 1000 / ClusterSize; i++) {
    const TTEntry *tte = &TT.table[i].entry[0];
    for (int j = 0; j < ClusterSize; j++)
      if ((tte[j].genBound8 & 0xFC) == TT.generation8)
        cnt++;
  }
  return cnt;
}
