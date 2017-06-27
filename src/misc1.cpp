// Copyright (c) 1989-2008 James E. Wilson, Robert A. Koeneke, David J. Grabiner
//
// Umoria is free software released under a GPL v2 license and comes with
// ABSOLUTELY NO WARRANTY. See https://www.gnu.org/licenses/gpl-2.0.html
// for further details.

// Misc utility and initialization code, magic objects code

#include "headers.h"
#include "externs.h"

// holds the previous rnd state
static uint32_t old_seed;

static void panel_bounds();
static int popm();
static int max_hp(uint8_t *);
static int get_mons_num(int);

// gets a new random seed for the random number generator
void init_seeds(uint32_t seed) {
    uint32_t clock_var;

    if (seed == 0) {
        clock_var = (uint32_t) time((time_t *) 0);
    } else {
        clock_var = seed;
    }

    magic_seed = (int32_t) clock_var;

    clock_var += 8762;
    town_seed = (int32_t) clock_var;

    clock_var += 113452L;
    set_rnd_seed(clock_var);

    // make it a little more random
    for (clock_var = (uint32_t) randint(100); clock_var != 0; clock_var--) {
        (void) rnd();
    }
}

// change to different random number generator state
void set_seed(uint32_t seed) {
    old_seed = get_rnd_seed();

    // want reproducible state here
    set_rnd_seed(seed);
}

// restore the normal random generator state
void reset_seed() {
    set_rnd_seed(old_seed);
}

// Generates a random integer x where 1<=X<=MAXVAL -RAK-
int randint(int maxval) {
    return (rnd() % maxval) + 1;
}

// Generates a random integer number of NORMAL distribution -RAK-
int randnor(int mean, int stand) {
    // alternate randnor code, slower but much smaller since no table
    // 2 per 1,000,000 will be > 4*SD, max is 5*SD
    //
    // tmp = damroll(8, 99);   // mean 400, SD 81
    // tmp = (tmp - 400) * stand / 81;
    // return tmp + mean;

    int tmp = randint(MAX_SHORT);

    // off scale, assign random value between 4 and 5 times SD
    if (tmp == MAX_SHORT) {
        int offset = 4 * stand + randint(stand);

        // one half are negative
        if (randint(2) == 1) {
            offset = -offset;
        }

        return mean + offset;
    }

    // binary search normal normal_table to get index that
    // matches tmp this takes up to 8 iterations.
    int low = 0;
    int iindex = NORMAL_TABLE_SIZE >> 1;
    int high = NORMAL_TABLE_SIZE;

    while (true) {
        if (normal_table[iindex] == tmp || high == low + 1) {
            break;
        }

        if (normal_table[iindex] > tmp) {
            high = iindex;
            iindex = low + ((iindex - low) >> 1);
        } else {
            low = iindex;
            iindex = iindex + ((high - iindex) >> 1);
        }
    }

    // might end up one below target, check that here
    if (normal_table[iindex] < tmp) {
        iindex = iindex + 1;
    }

    // normal_table is based on SD of 64, so adjust the
    // index value here, round the half way case up.
    int offset = ((stand * iindex) + (NORMAL_TABLE_SD >> 1)) / NORMAL_TABLE_SD;

    // one half should be negative
    if (randint(2) == 1) {
        offset = -offset;
    }

    return mean + offset;
}

// Returns position of first set bit -RAK-
// and clears that bit
int bit_pos(uint32_t *test) {
    uint32_t mask = 0x1;

    for (int i = 0; i < (int) sizeof(*test) * 8; i++) {
        if (*test & mask) {
            *test &= ~mask;
            return i;
        }
        mask <<= 1;
    }

    // no one bits found
    return -1;
}

// Checks a co-ordinate for in bounds status -RAK-
bool in_bounds(int y, int x) {
    bool validY = y > 0 && y < dungeon_height - 1;
    bool validX = x > 0 && x < dungeon_width - 1;

    return validY && validX;
}

// Calculates current boundaries -RAK-
static void panel_bounds() {
    panel_row_min = panel_row * (SCREEN_HEIGHT / 2);
    panel_row_max = panel_row_min + SCREEN_HEIGHT - 1;
    panel_row_prt = panel_row_min - 1;
    panel_col_min = panel_col * (SCREEN_WIDTH / 2);
    panel_col_max = panel_col_min + SCREEN_WIDTH - 1;
    panel_col_prt = panel_col_min - 13;
}

// Given an row (y) and col (x), this routine detects -RAK-
// when a move off the screen has occurred and figures new borders.
// Force, forces the panel bounds to be recalculated, useful for 'W'here.
int get_panel(int y, int x, bool force) {
    int row = panel_row;
    int col = panel_col;

    if (force || y < panel_row_min + 2 || y > panel_row_max - 2) {
        row = (y - SCREEN_HEIGHT / 4) / (SCREEN_HEIGHT / 2);

        if (row > max_panel_rows) {
            row = max_panel_rows;
        } else if (row < 0) {
            row = 0;
        }
    }

    if (force || x < panel_col_min + 3 || x > panel_col_max - 3) {
        col = ((x - SCREEN_WIDTH / 4) / (SCREEN_WIDTH / 2));
        if (col > max_panel_cols) {
            col = max_panel_cols;
        } else if (col < 0) {
            col = 0;
        }
    }

    bool panel = false;

    if (row != panel_row || col != panel_col) {
        panel_row = row;
        panel_col = col;
        panel_bounds();
        panel = true;

        // stop movement if any
        if (find_bound) {
            end_find();
        }
    }

    return panel;
}

// Tests a given point to see if it is within the screen -RAK-
// boundaries.
bool panel_contains(int y, int x) {
    bool validY = y >= panel_row_min && y <= panel_row_max;
    bool validX = x >= panel_col_min && x <= panel_col_max;

    return validY && validX;
}

// Distance between two points -RAK-
int distance(int y1, int x1, int y2, int x2) {
    int dy = y1 - y2;
    if (dy < 0) {
        dy = -dy;
    }

    int dx = x1 - x2;
    if (dx < 0) {
        dx = -dx;
    }

    int a = (dy + dx) << 1;
    int b = dy > dx ? dx : dy;

    return ((a - b) >> 1);
}

// Checks points north, south, east, and west for a wall -RAK-
// note that y,x is always in_bounds(), i.e. 0 < y < dungeon_height-1,
// and 0 < x < dungeon_width-1
int next_to_walls(int y, int x) {
    int walls = 0;

    if (cave[y - 1][x].fval >= MIN_CAVE_WALL) {
        walls++;
    }

    if (cave[y + 1][x].fval >= MIN_CAVE_WALL) {
        walls++;
    }

    if (cave[y][x - 1].fval >= MIN_CAVE_WALL) {
        walls++;
    }

    if (cave[y][x + 1].fval >= MIN_CAVE_WALL) {
        walls++;
    }

    return walls;
}

// Checks all adjacent spots for corridors -RAK-
// note that y, x is always in_bounds(), hence no need to check that
// j, k are in_bounds(), even if they are 0 or cur_x-1 is still works
int next_to_corr(int y, int x) {
    int walls = 0;

    for (int yy = y - 1; yy <= y + 1; yy++) {
        for (int xx = x - 1; xx <= x + 1; xx++) {
            int tileID = cave[yy][xx].fval;
            int treasureID = cave[yy][xx].tptr;

            // should fail if there is already a door present
            if (tileID == CORR_FLOOR && (treasureID == 0 || treasure_list[treasureID].tval < TV_MIN_DOORS)) {
                walls++;
            }
        }
    }

    return walls;
}

// generates damage for 2d6 style dice rolls
int damroll(int num, int sides) {
    int sum = 0;
    for (int i = 0; i < num; i++) {
        sum += randint(sides);
    }
    return sum;
}

int pdamroll(uint8_t *array) {
    return damroll((int) array[0], (int) array[1]);
}

// A simple, fast, integer-based line-of-sight algorithm.  By Joseph Hall,
// 4116 Brewster Drive, Raleigh NC 27606.  Email to jnh@ecemwl.ncsu.edu.
//
// Returns true if a line of sight can be traced from x0, y0 to x1, y1.
//
// The LOS begins at the center of the tile [x0, y0] and ends at the center of
// the tile [x1, y1].  If los() is to return true, all of the tiles this line
// passes through must be transparent, WITH THE EXCEPTIONS of the starting and
// ending tiles.
//
// We don't consider the line to be "passing through" a tile if it only passes
// across one corner of that tile.

// Because this function uses (short) ints for all calculations, overflow may
// occur if deltaX and deltaY exceed 90.
bool los(int fromY, int fromX, int toY, int toX) {
    int deltaX = toX - fromX;
    int deltaY = toY - fromY;

    // Adjacent?
    if (deltaX < 2 && deltaX > -2 && deltaY < 2 && deltaY > -2) {
        return true;
    }

    // Handle the cases where deltaX or deltaY == 0.
    if (deltaX == 0) {
        if (deltaY < 0) {
            int tmp = fromY;
            fromY = toY;
            toY = tmp;
        }

        for (int yy = fromY + 1; yy < toY; yy++) {
            if (cave[yy][fromX].fval >= MIN_CLOSED_SPACE) {
                return false;
            }
        }

        return true;
    }

    if (deltaY == 0) {
        if (deltaX < 0) {
            int tmp = fromX;
            fromX = toX;
            toX = tmp;
        }

        for (int xx = fromX + 1; xx < toX; xx++) {
            if (cave[fromY][xx].fval >= MIN_CLOSED_SPACE) {
                return false;
            }
        }

        return true;
    }

    // Now, we've eliminated all the degenerate cases.
    // In the computations below, dy (or dx) and m are multiplied by a scale factor,
    // scale = abs(deltaX * deltaY * 2), so that we can use integer arithmetic.
    {
        int xx;         // x position
        int yy;         // y position
        int scale;      // above scale factor
        int scaleHalf;  // above scale factor / 2
        int xSign;      // sign of deltaX
        int ySign;      // sign of deltaY
        int slope;      // slope or 1/slope of LOS

        scaleHalf = abs(deltaX * deltaY);
        scale = scaleHalf << 1;
        xSign = deltaX < 0 ? -1 : 1;
        ySign = deltaY < 0 ? -1 : 1;

        // Travel from one end of the line to the other, oriented along the longer axis.

        if (abs(deltaX) >= abs(deltaY)) {
            int dy; // "fractional" y position

            // We start at the border between the first and second tiles, where
            // the y offset = .5 * slope.  Remember the scale factor.
            //
            // We have:     slope = deltaY / deltaX * 2 * (deltaY * deltaX)
            //                    = 2 * deltaY * deltaY.

            dy = deltaY * deltaY;
            slope = dy << 1;
            xx = fromX + xSign;

            // Consider the special case where slope == 1.
            if (dy == scaleHalf) {
                yy = fromY + ySign;
                dy -= scale;
            } else {
                yy = fromY;
            }

            while (toX - xx) {
                if (cave[yy][xx].fval >= MIN_CLOSED_SPACE) {
                    return false;
                }

                dy += slope;

                if (dy < scaleHalf) {
                    xx += xSign;
                } else if (dy > scaleHalf) {
                    yy += ySign;
                    if (cave[yy][xx].fval >= MIN_CLOSED_SPACE) {
                        return false;
                    }
                    xx += xSign;
                    dy -= scale;
                } else {
                    // This is the case, dy == scaleHalf, where the LOS
                    // exactly meets the corner of a tile.
                    xx += xSign;
                    yy += ySign;
                    dy -= scale;
                }
            }
            return true;
        }

        int dx; // "fractional" x position

        dx = deltaX * deltaX;
        slope = dx << 1;

        yy = fromY + ySign;

        if (dx == scaleHalf) {
            xx = fromX + xSign;
            dx -= scale;
        } else {
            xx = fromX;
        }

        while (toY - yy) {
            if (cave[yy][xx].fval >= MIN_CLOSED_SPACE) {
                return false;
            }

            dx += slope;

            if (dx < scaleHalf) {
                yy += ySign;
            } else if (dx > scaleHalf) {
                xx += xSign;
                if (cave[yy][xx].fval >= MIN_CLOSED_SPACE) {
                    return false;
                }
                yy += ySign;
                dx -= scale;
            } else {
                xx += xSign;
                yy += ySign;
                dx -= scale;
            }
        }
        return true;
    }
}

// Returns symbol for given row, column -RAK-
char loc_symbol(int y, int x) {
    Cave_t *cave_ptr = &cave[y][x];

    if (cave_ptr->cptr == 1 && (!running_counter || run_print_self)) {
        return '@';
    }

    if (py.flags.status & PY_BLIND) {
        return ' ';
    }

    if (py.flags.image > 0 && randint(12) == 1) {
        return (uint8_t) (randint(95) + 31);
    }

    if (cave_ptr->cptr > 1 && monsters_list[cave_ptr->cptr].ml) {
        return creatures_list[monsters_list[cave_ptr->cptr].mptr].cchar;
    }

    if (!cave_ptr->pl && !cave_ptr->tl && !cave_ptr->fm) {
        return ' ';
    }

    if (cave_ptr->tptr != 0 && treasure_list[cave_ptr->tptr].tval != TV_INVIS_TRAP) {
        return treasure_list[cave_ptr->tptr].tchar;
    }

    if (cave_ptr->fval <= MAX_CAVE_FLOOR) {
        return '.';
    }

    if (cave_ptr->fval == GRANITE_WALL || cave_ptr->fval == BOUNDARY_WALL || !highlight_seams) {
        return '#';
    }

    // Originally set highlight bit, but that is not portable,
    // now use the percent sign instead.
    return '%';
}

// Tests a spot for light or field mark status -RAK-
bool test_light(int y, int x) {
    return cave[y][x].pl || cave[y][x].tl || cave[y][x].fm;
}

// Prints the map of the dungeon -RAK-
void prt_map() {
    int line = 1;

    // Top to bottom
    for (int y = panel_row_min; y <= panel_row_max; y++) {
        erase_line(line++, 13);

        // Left to right
        for (int x = panel_col_min; x <= panel_col_max; x++) {
            char ch = loc_symbol(y, x);
            if (ch != ' ') {
                print(ch, y, x);
            }
        }
    }
}

// Compact monsters -RAK-
// Return true if any monsters were deleted, false if could not delete any monsters.
bool compact_monsters() {
    msg_print("Compacting monsters...");

    int cur_dis = 66;

    bool delete_any = false;
    while (!delete_any) {
        for (int i = next_free_monster_id - 1; i >= MIN_MONIX; i--) {
            if (cur_dis < monsters_list[i].cdis && randint(3) == 1) {
                if (creatures_list[monsters_list[i].mptr].cmove & CM_WIN) {
                    // Never compact away the Balrog!!
                } else if (hack_monptr < i) {
                    // in case this is called from within creatures(), this is a horrible
                    // hack, the monsters_list/creatures() code needs to be rewritten.
                    delete_monster(i);
                    delete_any = true;
                } else {
                    // fix1_delete_monster() does not decrement next_free_monster_id,
                    // so don't set delete_any if this was called.
                    fix1_delete_monster(i);
                }
            }
        }

        if (!delete_any) {
            cur_dis -= 6;

            // Can't delete any monsters, return failure.
            if (cur_dis < 0) {
                return false;
            }
        }
    }

    return true;
}

// Add to the players food time -RAK-
void add_food(int num) {
    if (py.flags.food < 0) {
        py.flags.food = 0;
    }

    py.flags.food += num;

    if (py.flags.food > PLAYER_FOOD_MAX) {
        msg_print("You are bloated from overeating.");

        // Calculate how much of num is responsible for the bloating. Give the
        // player food credit for 1/50, and slow him for that many turns also.
        int extra = py.flags.food - PLAYER_FOOD_MAX;
        if (extra > num) {
            extra = num;
        }
        int penalty = extra / 50;

        py.flags.slow += penalty;

        if (extra == num) {
            py.flags.food = (int16_t) (py.flags.food - num + penalty);
        } else {
            py.flags.food = (int16_t) (PLAYER_FOOD_MAX + penalty);
        }
    } else if (py.flags.food > PLAYER_FOOD_FULL) {
        msg_print("You are full.");
    }
}

// Returns a pointer to next free space -RAK-
// Returns -1 if could not allocate a monster.
static int popm() {
    if (next_free_monster_id == MAX_MALLOC) {
        if (!compact_monsters()) {
            return -1;
        }
    }
    return next_free_monster_id++;
}

// Gives Max hit points -RAK-
static int max_hp(uint8_t *array) {
    return (array[0] * array[1]);
}

// Places a monster at given location -RAK-
bool place_monster(int y, int x, int monsterID, bool slp) {
    int cur_pos = popm();

    if (cur_pos == -1) {
        return false;
    }

    Monster_t *mon_ptr = &monsters_list[cur_pos];

    mon_ptr->fy = (uint8_t) y;
    mon_ptr->fx = (uint8_t) x;
    mon_ptr->mptr = (uint16_t) monsterID;

    if (creatures_list[monsterID].cdefense & CD_MAX_HP) {
        mon_ptr->hp = (int16_t) max_hp(creatures_list[monsterID].hd);
    } else {
        mon_ptr->hp = (int16_t) pdamroll(creatures_list[monsterID].hd);
    }

    // the creatures_list speed value is 10 greater, so that it can be a uint8_t
    mon_ptr->cspeed = (int16_t) (creatures_list[monsterID].speed - 10 + py.flags.speed);
    mon_ptr->stunned = 0;
    mon_ptr->cdis = (uint8_t) distance(char_row, char_col, y, x);
    mon_ptr->ml = false;

    cave[y][x].cptr = (uint8_t) cur_pos;

    if (slp) {
        if (creatures_list[monsterID].sleep == 0) {
            mon_ptr->csleep = 0;
        } else {
            mon_ptr->csleep = (int16_t) ((creatures_list[monsterID].sleep * 2) + randint((int) creatures_list[monsterID].sleep * 10));
        }
    } else {
        mon_ptr->csleep = 0;
    }

    return true;
}

// Places a monster at given location -RAK-
void place_win_monster() {
    if (total_winner) {
        return;
    }

    int cur_pos = popm();

    // Check for case where could not allocate space for
    // the win monster, this should never happen.
    if (cur_pos == -1) {
        abort();
    }

    Monster_t *mon_ptr = &monsters_list[cur_pos];

    int y, x;
    do {
        y = randint(dungeon_height - 2);
        x = randint(dungeon_width - 2);
    } while ((cave[y][x].fval >= MIN_CLOSED_SPACE) || (cave[y][x].cptr != 0) || (cave[y][x].tptr != 0) || (distance(y, x, char_row, char_col) <= MAX_SIGHT));

    mon_ptr->fy = (uint8_t) y;
    mon_ptr->fx = (uint8_t) x;
    mon_ptr->mptr = (uint16_t) (randint(WIN_MON_TOT) - 1 + monster_levels[MAX_MONS_LEVEL]);

    if (creatures_list[mon_ptr->mptr].cdefense & CD_MAX_HP) {
        mon_ptr->hp = (int16_t) max_hp(creatures_list[mon_ptr->mptr].hd);
    } else {
        mon_ptr->hp = (int16_t) pdamroll(creatures_list[mon_ptr->mptr].hd);
    }

    // the creatures_list speed value is 10 greater, so that it can be a uint8_t
    mon_ptr->cspeed = (int16_t) (creatures_list[mon_ptr->mptr].speed - 10 + py.flags.speed);
    mon_ptr->stunned = 0;
    mon_ptr->cdis = (uint8_t) distance(char_row, char_col, y, x);

    cave[y][x].cptr = (uint8_t) cur_pos;

    mon_ptr->csleep = 0;
}

// Return a monster suitable to be placed at a given level. This
// makes high level monsters (up to the given level) slightly more
// common than low level monsters at any given level. -CJS-
static int get_mons_num(int level) {
    if (level == 0) {
        return randint(monster_levels[0]) - 1;
    }

    if (level > MAX_MONS_LEVEL) {
        level = MAX_MONS_LEVEL;
    }

    if (randint(MON_NASTY) == 1) {
        level = level + abs(randnor(0, 4)) + 1;

        if (level > MAX_MONS_LEVEL) {
            level = MAX_MONS_LEVEL;
        }
    } else {
        // This code has been added to make it slightly more likely to get
        // the higher level monsters. Originally a uniform distribution over
        // all monsters of level less than or equal to the dungeon level.
        // This distribution makes a level n monster occur approx 2/n% of the
        // time on level n, and 1/n*n% are 1st level.
        int num = monster_levels[level] - monster_levels[0];
        int i = randint(num) - 1;
        int j = randint(num) - 1;
        if (j > i) {
            i = j;
        }
        level = creatures_list[i + monster_levels[0]].level;
    }

    return randint(monster_levels[level] - monster_levels[level - 1]) - 1 + monster_levels[level - 1];
}

// Allocates a random monster -RAK-
void alloc_monster(int num, int dis, bool slp) {
    int y, x;

    for (int i = 0; i < num; i++) {
        do {
            y = randint(dungeon_height - 2);
            x = randint(dungeon_width - 2);
        } while (cave[y][x].fval >= MIN_CLOSED_SPACE || (cave[y][x].cptr != 0) || (distance(y, x, char_row, char_col) <= dis));

        int l = get_mons_num(current_dungeon_level);

        // Dragons are always created sleeping here,
        // so as to give the player a sporting chance.
        if (creatures_list[l].cchar == 'd' || creatures_list[l].cchar == 'D') {
            slp = true;
        }

        // Place_monster() should always return true here.
        // It does not matter if it fails though.
        (void) place_monster(y, x, l, slp);
    }
}

static bool placeMonsterAdjacentTo(int monsterID, int *y, int *x, bool slp) {
    bool placed = false;

    for (int i = 0; i <= 9; i++) {
        int yy = *y - 2 + randint(3);
        int xx = *x - 2 + randint(3);

        if (in_bounds(yy, xx)) {
            if (cave[yy][xx].fval <= MAX_OPEN_SPACE && cave[yy][xx].cptr == 0) {
                // Place_monster() should always return true here.
                if (!place_monster(yy, xx, monsterID, slp)) {
                    return false;
                }

                *y = yy;
                *x = xx;

                placed = true;
                i = 9;
            }
        }
    }

    return placed;
}

// Places creature adjacent to given location -RAK-
bool summon_monster(int *y, int *x, bool slp) {
    int monsterID = get_mons_num(current_dungeon_level + MON_SUMMON_ADJ);
    return placeMonsterAdjacentTo(monsterID, y, x, slp);
}

// Places undead adjacent to given location -RAK-
bool summon_undead(int *y, int *x) {
    int monsterID;

    int maxLevels = monster_levels[MAX_MONS_LEVEL];

    do {
        monsterID = randint(maxLevels) - 1;
        for (int i = 0; i <= 19;) {
            if (creatures_list[monsterID].cdefense & CD_UNDEAD) {
                i = 20;
                maxLevels = 0;
            } else {
                monsterID++;
                if (monsterID > maxLevels) {
                    i = 20;
                } else {
                    i++;
                }
            }
        }
    } while (maxLevels != 0);

    return placeMonsterAdjacentTo(monsterID, y, x, false);
}

// If too many objects on floor level, delete some of them-RAK-
static void compact_objects() {
    msg_print("Compacting objects...");

    int counter = 0;
    int cur_dis = 66;

    while (counter <= 0) {
        for (int y = 0; y < dungeon_height; y++) {
            for (int x = 0; x < dungeon_width; x++) {
                if (cave[y][x].tptr != 0 && distance(y, x, char_row, char_col) > cur_dis) {
                    int chance;

                    switch (treasure_list[cave[y][x].tptr].tval) {
                        case TV_VIS_TRAP:
                            chance = 15;
                            break;
                        case TV_INVIS_TRAP:
                        case TV_RUBBLE:
                        case TV_OPEN_DOOR:
                        case TV_CLOSED_DOOR:
                            chance = 5;
                            break;
                        case TV_UP_STAIR:
                        case TV_DOWN_STAIR:
                        case TV_STORE_DOOR:
                            // Stairs, don't delete them.
                            // Shop doors, don't delete them.
                            chance = 0;
                            break;
                        case TV_SECRET_DOOR: // secret doors
                            chance = 3;
                            break;
                        default:
                            chance = 10;
                    }
                    if (randint(100) <= chance) {
                        (void) delete_object(y, x);
                        counter++;
                    }
                }
            }
        }

        if (counter == 0) {
            cur_dis -= 6;
        }
    }

    if (cur_dis < 66) {
        prt_map();
    }
}

// Gives pointer to next free space -RAK-
int popt() {
    if (current_treasure_id == MAX_TALLOC) {
        compact_objects();
    }

    return current_treasure_id++;
}

// Pushs a record back onto free space list -RAK-
// Delete_object() should always be called instead, unless the object
// in question is not in the dungeon, e.g. in store1.c and files.c
void pusht(uint8_t treasureID) {
    if (treasureID != current_treasure_id - 1) {
        treasure_list[treasureID] = treasure_list[current_treasure_id - 1];

        // must change the tptr in the cave of the object just moved
        for (int y = 0; y < dungeon_height; y++) {
            for (int x = 0; x < dungeon_width; x++) {
                if (cave[y][x].tptr == current_treasure_id - 1) {
                    cave[y][x].tptr = treasureID;
                }
            }
        }
    }
    current_treasure_id--;

    invcopy(&treasure_list[current_treasure_id], OBJ_NOTHING);
}

// Should the object be enchanted -RAK-
bool magik(int chance) {
    return randint(100) <= chance;
}

// Enchant a bonus based on degree desired -RAK-
int m_bonus(int base, int max_std, int level) {
    int stand_dev = (OBJ_STD_ADJ * level / 100) + OBJ_STD_MIN;

    // Check for level > max_std since that may have generated an overflow.
    if (stand_dev > max_std || level > max_std) {
        stand_dev = max_std;
    }

    // abs may be a macro, don't call it with randnor as a parameter
    int tmp = randnor(0, stand_dev);
    int x = (abs(tmp) / 10) + base;

    if (x < base) {
        return base;
    }

    return x;
}
