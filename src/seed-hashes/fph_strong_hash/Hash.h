#pragma once

#include "fph/dynamic_fph_table.h"

static const char* HASH_NAME = "fph::StrongSeedHash";

template<class Key>
using Hash = fph::StrongSeedHash<Key>;