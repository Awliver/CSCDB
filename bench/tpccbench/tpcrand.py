"""TPC-C random distributions (spec clause 4.3.2).

NURand(A, x, y) = (((random(0,A) | random(x,y)) + C) % (y-x+1)) + x
C is a per-A runtime constant. For C_LAST the load-time and run-time constants
must differ by a valid delta (spec 2.1.6.1): 65..119, excluding 96 and 112.
"""

import random
import string

SYLLABLES = ["BAR", "OUGHT", "ABLE", "PRI", "PRES",
             "ESE", "ANTI", "CALLY", "ATION", "EING"]

A_C_LAST = 255
A_C_ID = 1023
A_OL_I_ID = 8191


def lastname(num):
    """Spec 4.3.2.3: three syllables from digits of num (0..999)."""
    return SYLLABLES[num // 100] + SYLLABLES[(num // 10) % 10] + SYLLABLES[num % 10]


def valid_c_delta(delta):
    return 65 <= delta <= 119 and delta not in (96, 112)


def derive_c_run(c_load, rng):
    """Pick a run-time C_LAST constant with a valid delta from the load-time one."""
    choices = [c for c in range(256) if valid_c_delta(abs(c - c_load))]
    return rng.choice(choices)


class TpccRandom:
    """Random source with spec-mandated distributions. One instance per thread."""

    def __init__(self, seed, c_last=None, c_id=None, c_ol_i_id=None):
        self.rng = random.Random(seed)
        self.c_last = c_last if c_last is not None else self.rng.randint(0, A_C_LAST)
        self.c_id = c_id if c_id is not None else self.rng.randint(0, A_C_ID)
        self.c_ol_i_id = c_ol_i_id if c_ol_i_id is not None else self.rng.randint(0, A_OL_I_ID)

    def randint(self, lo, hi):
        return self.rng.randint(lo, hi)

    def uniform(self, lo, hi):
        return self.rng.uniform(lo, hi)

    def choice(self, seq):
        return self.rng.choice(seq)

    def _nurand(self, a, c, x, y):
        return (((self.rng.randint(0, a) | self.rng.randint(x, y)) + c) % (y - x + 1)) + x

    def nurand_c_id(self, n=3000):
        return self._nurand(A_C_ID, self.c_id, 1, n)

    def nurand_i_id(self, n_items=100000):
        return self._nurand(A_OL_I_ID, self.c_ol_i_id, 1, n_items)

    def nurand_c_last(self):
        return lastname(self._nurand(A_C_LAST, self.c_last, 0, 999))

    # ---- string generators (spec 4.3.2.2) -----------------------------------
    _ALNUM = string.ascii_uppercase + string.ascii_lowercase + string.digits

    def astring(self, lo, hi):
        n = self.rng.randint(lo, hi)
        return "".join(self.rng.choice(self._ALNUM) for _ in range(n))

    def nstring(self, lo, hi):
        n = self.rng.randint(lo, hi)
        return "".join(self.rng.choice(string.digits) for _ in range(n))

    def zip_code(self):
        """Spec 4.3.2.7: 4 random digits + '11111'."""
        return self.nstring(4, 4) + "11111"

    def data_string(self, lo, hi, original_pct=10):
        """i_data/s_data: astring, 10% contain 'ORIGINAL' at a random position."""
        s = self.astring(lo, hi)
        if self.rng.randint(1, 100) <= original_pct:
            if len(s) < 8:
                return "ORIGINAL"
            pos = self.rng.randint(0, len(s) - 8)
            s = s[:pos] + "ORIGINAL" + s[pos + 8:]
        return s

    def money(self, lo, hi):
        return round(self.rng.uniform(lo, hi), 2)

    def shuffled(self, seq):
        seq = list(seq)
        self.rng.shuffle(seq)
        return seq
