#include <cstdio>
#include <unordered_set>
#include <unordered_map>
#include <array>
#include "Map.h"
// for random generator
#include "fph/dynamic_fph_table.h"




#ifndef FPH_HAVE_BUILTIN
#ifdef __has_builtin
#define FPH_HAVE_BUILTIN(x) __has_builtin(x)
#else
#define FPH_HAVE_BUILTIN(x) 0
#endif
#endif

#ifndef FPH_HAS_BUILTIN_OR_GCC_CLANG
#if defined(__GNUC__) || defined(__clang__)
#   define FPH_HAS_BUILTIN_OR_GCC_CLANG(x) 1
#else
#   define FPH_HAS_BUILTIN_OR_GCC_CLANG(x) FPH_HAVE_BUILTIN(x)
#endif
#endif

#ifndef FPH_LIKELY
#ifndef FPH_LIKELY
#   if FPH_HAS_BUILTIN_OR_GCC_CLANG(__builtin_expect)
#       define FPH_LIKELY(x) (__builtin_expect((x), 1))
#   else
#       define FPH_LIKELY(x) (x)
#   endif
#endif
#endif

#ifndef FPH_UNLIKELY
#ifndef FPH_UNLIKELY
#   if FPH_HAS_BUILTIN_OR_GCC_CLANG(__builtin_expect)
#       define FPH_UNLIKELY(x) (__builtin_expect((x), 0))
#   else
#       define FPH_UNLIKELY(x) (x)
#   endif
#endif
#endif

#if defined(_WIN32) || defined(_WIN64)
#define BENCH_OS_WIN
#include <windows.h>  // NOLINT
#endif

#if defined(__APPLE__)
#define BENCH_OS_X
#endif


// from absl
// Prevents the compiler from eliding the computations that led to "output".
template <class T>
inline void PreventElision(T&& output) {
#ifndef BENCH_OS_WIN
    // Works by indicating to the compiler that "output" is being read and
    // modified. The +r constraint avoids unnecessary writes to memory, but only
    // works for built-in types (typically FuncOutput).
    asm volatile("" :: "g" (output));
#else
#include <atomic>
    // MSVC does not support inline assembly anymore (and never supported GCC's
  // RTL constraints). Self-assignment with #pragma optimize("off") might be
  // expected to prevent elision, but it does not with MSVC 2015. Type-punning
  // with volatile pointers generates inefficient code on MSVC 2017.
  static std::atomic<T> dummy(T{});
  dummy.store(output, std::memory_order_relaxed);
#endif
}


template <typename>
struct is_pair : std::false_type
{ };

template <typename T, typename U>
struct is_pair<std::pair<T, U>> : std::true_type
{ };

template<class T, typename = void>
struct SimpleGetKey {
    const T& operator()(const T& x) const {
        return x;
    }

    T& operator()(T& x) {
        return x;
    }

    T operator()(T&& x) {
        return std::move(x);
    }

};

template<class T>
struct SimpleGetKey<T, typename std::enable_if<is_pair<T>::value, void>::type> {

    using key_type = typename T::first_type;


    const key_type& operator()(const T& x) const {
        return x.first;
    }

    key_type& operator()(T& x) {
        return x.first;
    }

    key_type operator()(T&& x) {
        return std::move(x.first);
    }
};

template<typename T, typename = void>
struct MutableValue {
    using type = T;
};

template<typename T>
struct MutableValue<T, typename std::enable_if<is_pair<T>::value>::type> {
    using type = std::pair<typename std::remove_const<typename T::first_type>::type, typename T::second_type>;
};

template<class T>
std::string ToString(const T& t) {
    return std::to_string(t);
}


std::string ToString(const std::string& t) {
    return t;
}

std::string ToString(const char* src) {
    return src;
}

template<class Table, class PairVec, class GetKey = SimpleGetKey<typename PairVec::value_type>>
bool ConstructTable(Table &table, const PairVec &pair_vec, bool do_reserve = true, bool do_rehash = false) {
    table.clear();

    if (do_reserve) {
        table.reserve(pair_vec.size());
    }
    for (size_t i = 0; i < pair_vec.size();) {
        const auto &pair = pair_vec[i++];
        table.insert(pair);
    }
    if (do_rehash) {
        if (table.load_factor() < 0.45) {
            table.max_load_factor(0.9);
            table.rehash(table.size());
        }
    }
    return true;


}

template<bool do_reserve = true, bool verbose = true, class Table, class PairVec,
        class GetKey = SimpleGetKey<typename PairVec::value_type>>
uint64_t TestTableConstruct(Table &table, const PairVec &pair_vec) {
    auto begin_time = std::chrono::high_resolution_clock::now();
    if constexpr (do_reserve) {
        table.reserve(pair_vec.size());
    }
    for (size_t i = 0; i < pair_vec.size();) {
        table.insert(pair_vec[i++]);
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    auto pass_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - begin_time).count();
    if constexpr (verbose) {
        fprintf(stderr, "%s construct use time %.6f seconds",
                MAP_NAME, (double)pass_ns / (1e+9));
    }
    return pass_ns;
}

enum LookupExpectation {
    KEY_IN = 0,
    KEY_NOT_IN,
    KEY_MAY_IN,
};

template<class Table,  class PairVec, class ValueGen, class GetKey = SimpleGetKey<typename Table::value_type>>
std::tuple<uint64_t> TestTableEraseAndInsertImp(const PairVec& src_vec, ValueGen& miss_value_gen,
                                             size_t erase_time, size_t seed) {
    Table table;
    ConstructTable(table, src_vec, true, false);
    using value_type = typename MutableValue<typename Table::value_type>::type;
    std::vector<value_type> element_vec(table.begin(), table.end());
    size_t cur_cnt = element_vec.size();

    std::minstd_rand uint_engine(seed);
    std::uniform_int_distribution<size_t> rand_dis;

    std::vector<size_t> size_t_vec(erase_time);
    for (size_t t = 0; t < erase_time; ++t) {
        size_t_vec[t] = rand_dis(uint_engine) % cur_cnt;
    }
    const size_t most_possible_insert_cnt = erase_time;
    std::vector<value_type> new_vec;
    new_vec.reserve(most_possible_insert_cnt);
    for (size_t i = 0; i < most_possible_insert_cnt; ++i) {
        new_vec.emplace_back(miss_value_gen());
    }
    element_vec.template emplace_back(miss_value_gen());

    auto start_t = std::chrono::high_resolution_clock::now();
    for (size_t t = 0; t < erase_time; ++t) {

        auto [it, ok] = table.emplace(new_vec[t]);
        if FPH_LIKELY(ok) {

            size_t erase_index = size_t_vec[t];
            table.erase(GetKey{}(element_vec[erase_index]));
            element_vec[erase_index] = new_vec[t];
        }
    }
    auto end_t = std::chrono::high_resolution_clock::now();
    uint64_t pass_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_t - start_t).count();
    return {pass_ns};
}

template<class Table, class PairVec, class ValueGen, class GetKey = SimpleGetKey<typename PairVec::value_type>>
std::tuple<uint64_t> TestTableEraseAndInsert(ValueGen& miss_value_gen,
                                             const PairVec &src_vec,
                                             size_t erase_time, size_t seed) {


    std::mt19937_64 uint_engine(seed);
    std::uniform_int_distribution<size_t> rand_dis;

    const size_t test_timeout_erase_cnt = 10000ULL;
    const size_t test_timeout_total_op_cnt = test_timeout_erase_cnt * 2ULL;
    constexpr int64_t timeout_per_op_ns = 4000LL; // 1400 ns per operation
    const uint64_t timeout_total_threshold = test_timeout_total_op_cnt * timeout_per_op_ns;

    auto [test_timeout_ns] = TestTableEraseAndInsertImp<Table>(src_vec, miss_value_gen,
                                                    test_timeout_erase_cnt, rand_dis(uint_engine));
    if (test_timeout_ns > timeout_total_threshold) {
        fprintf(stderr, "Timeout when in testing EraseAndInsert, avg erase and insert op use %.4f ns\n",
                double(test_timeout_ns) / double(test_timeout_total_op_cnt));
        return {0};
    }
    return TestTableEraseAndInsertImp<Table>
                    (src_vec, miss_value_gen, erase_time, rand_dis(uint_engine));

}


template<LookupExpectation LOOKUP_EXP, bool verbose = true, class Table, class PairVec,
        class GetKey = SimpleGetKey<typename PairVec::value_type> >
std::tuple<uint64_t, uint64_t> TestTableLookUp(Table &table, size_t lookup_time, const PairVec &input_vec,
                                               const PairVec &lookup_vec, size_t seed,
                                               bool set_load_factor = false) {

    size_t look_up_index = 0;
    size_t key_num = input_vec.size();
    uint64_t useless_sum = 0;
    if (input_vec.empty()) {
        return {0, 0};
    }
    std::mt19937_64 random_engine(seed);
    std::uniform_int_distribution<size_t> random_dis;
    auto pair_vec = lookup_vec;
    auto start_construct_t = std::chrono::high_resolution_clock::now();
    try {
        ConstructTable<Table, PairVec, GetKey>(table, input_vec, true, set_load_factor);
    } catch(std::exception& e) {
        fprintf(stderr, "Catch exception in Construct when TestTableLookUp, set_max_load_factor: %d, msg:%s\n",
                set_load_factor, e.what());
        return {0, 0};
    }
    auto end_construct_t = std::chrono::high_resolution_clock::now();
    auto construct_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_construct_t - start_construct_t).count();
    std::shuffle(pair_vec.begin(), pair_vec.end(), random_engine);

//    size_t test_timeout_cnt = 0;
    {
        constexpr int64_t per_find_timeout_threshold_ns = 1500LL; // 1500 ns per call
        const size_t timeout_test_lookup_cnt = 100000LL;
        const int64_t total_timeout_threshold_ns = per_find_timeout_threshold_ns * timeout_test_lookup_cnt;

        auto timeout_test_start_t = std::chrono::high_resolution_clock::now();
        for (size_t t = 0; t < timeout_test_lookup_cnt; ++t) {
            ++look_up_index;
            if FPH_UNLIKELY(look_up_index >= key_num) {
                look_up_index -= key_num;
            }
            PreventElision(table.find(GetKey{}(pair_vec[look_up_index])));
        }
        auto timeout_test_end_t = std::chrono::high_resolution_clock::now();
        auto test_pass_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout_test_end_t
                                - timeout_test_start_t).count();
        if (test_pass_ns > total_timeout_threshold_ns) {
            fprintf(stderr, "Time out in test lookup, %s with %s use %.3f ns in the initial find test\n",
                    MAP_NAME, HASH_NAME, double(test_pass_ns) / double(timeout_test_lookup_cnt));
            return {0,0};
        }
    }

    constexpr int64_t look_up_timeout_threshold_ns = 1'000'000'000LL * 120LL; // 120 sec timeout
    const size_t one_sub_lookup_cnt = std::min(lookup_time / 10ULL, 10000000ULL);
    const size_t sub_lookup_task_cnt = (lookup_time + one_sub_lookup_cnt - 1ULL ) / one_sub_lookup_cnt;
    const size_t last_sub_lookup_cnt = lookup_time - (sub_lookup_task_cnt - 1ULL) * one_sub_lookup_cnt;

    int64_t total_sub_task_ns = 0;

    for (size_t k = 0; k < sub_lookup_task_cnt; ++k) {
        size_t sub_lookup_time = k == sub_lookup_task_cnt - 1ULL ? last_sub_lookup_cnt : one_sub_lookup_cnt;
        auto sub_start_t = std::chrono::high_resolution_clock::now();
        for (size_t t = 0; t < sub_lookup_time; ++t) {
            ++look_up_index;
            if FPH_UNLIKELY(look_up_index >= key_num) {
                look_up_index -= key_num;
            }
            PreventElision(table.find(GetKey{}(pair_vec[look_up_index])));
        }
        auto sub_end_t = std::chrono::high_resolution_clock::now();
        auto sub_pass_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(sub_end_t - sub_start_t).count();
        total_sub_task_ns += sub_pass_ns;
        if FPH_UNLIKELY(total_sub_task_ns > look_up_timeout_threshold_ns) {
            fprintf(stderr, "Timeout when test lookup %s with %s\n", MAP_NAME, HASH_NAME);
            return {0,0};
        }

    }
    auto look_up_ns = total_sub_task_ns;
//    auto look_up_t0 = std::chrono::high_resolution_clock::now();
//    for (size_t t = 1; t <= lookup_time; ++t) {
//        ++look_up_index;
//        if FPH_UNLIKELY(look_up_index >= key_num) {
//            look_up_index -= key_num;
//        }
//        PreventElision(table.find(GetKey{}(pair_vec[look_up_index])));
//
//
//    }
//    auto look_up_t1 = std::chrono::high_resolution_clock::now();
//    auto look_up_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(look_up_t1 - look_up_t0).count();
    if constexpr (verbose) {
        fprintf(stderr, "%s look up use %.3f ns per call, use_less sum: %lu",
                       MAP_NAME, (double)look_up_ns * 1.0 / (double)lookup_time,
                       useless_sum);
    }
    return {look_up_ns, construct_ns};
}

template<class Table, class PairVec,
        class GetKey = SimpleGetKey<typename PairVec::value_type> >
std::tuple<uint64_t, uint64_t> TestTableIterate(Table &table, size_t iterate_time, const PairVec &input_vec) {
    try {
        ConstructTable<Table, PairVec, GetKey>(table, input_vec);
    }   catch(std::exception &e) {
        fprintf(stderr, "Catch exception when TestTableIterate, hash:%s, map:%s\n%s\n",
                HASH_NAME, MAP_NAME, e.what());
        return {0, 0};
    }
    auto start_time = std::chrono::high_resolution_clock::now();
    uint64_t useless_sum = 0;
    for (size_t t = 0; t < iterate_time; ++t) {
        for (auto it = table.begin(); it != table.end(); ++it) {
            // If second is not int
            PreventElision(std::addressof(it->second));
//            useless_sum += *reinterpret_cast<uint8_t*>(std::addressof(it->second));
        }
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    uint64_t pass_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
    return {pass_ns, useless_sum};

}

template<class T, class RNG>
void ConstructRngPtr(std::unique_ptr<RNG>& key_rng_ptr, size_t seed, size_t max_possible_value) {
    if constexpr(std::is_integral_v<T>) {
        key_rng_ptr = std::make_unique<RNG>(seed, max_possible_value);
    }
    else {
        key_rng_ptr = std::make_unique<RNG>(seed);
    }
}

//using StatsTuple = std::tuple<double, double, double, double, double, double, double, double, double>;
using StatsTuple = std::array<double, 13>;
using TimeoutFlagArr = std::array<bool, std::tuple_size_v<StatsTuple>>;
static constexpr std::array<size_t, 7> check_timeout_index_arr = {2, 5, 6, 7, 8, 9, 10};

template<class ValueRandomGen, class Table, class value_type,
        class GetKey = SimpleGetKey<value_type>>
StatsTuple TestTablePerformance(size_t element_num, size_t construct_time, size_t lookup_time,
                          size_t erase_time, const TimeoutFlagArr& timeout_flag_arr, size_t seed = 0) {
    std::mt19937_64 random_engine(seed);
    std::uniform_int_distribution<size_t> size_gen;
    using mutable_value_type = typename MutableValue<value_type>::type;

    using key_type = typename Table::key_type;
    using mapped_type = typename Table::mapped_type;

    using KeyRNG = typename ValueRandomGen::KeyRNGType;
    using ValueRNG = typename ValueRandomGen::ValueRNGType;

    std::unique_ptr<KeyRNG> key_rng_ptr;
    std::unique_ptr<ValueRNG> value_rng_ptr;
    ConstructRngPtr<key_type, KeyRNG>(key_rng_ptr, seed, element_num);
    ConstructRngPtr<mapped_type, ValueRNG>(value_rng_ptr, seed, element_num);
    ValueRandomGen value_gen{seed, std::move(*key_rng_ptr), std::move(*value_rng_ptr)};
    value_gen.seed(seed);

    std::unordered_set<key_type> key_set;
    key_set.reserve(element_num);

    std::vector<mutable_value_type> src_vec;
    src_vec.reserve(element_num);
    for (size_t i = 0; i < element_num; ++i) {
        auto temp_pair = value_gen();
        while (key_set.find(GetKey{}(temp_pair)) != key_set.end()) {
            temp_pair = value_gen();
        }
        src_vec.push_back(temp_pair);
        key_set.insert(GetKey{}(temp_pair));
    }



    size_t construct_seed = size_gen(random_engine);
    size_t total_reserve_construct_ns = 0;

    // Test insert with reserve
    try {
#ifdef BENCH_ONLY_STRING
        constexpr uint64_t timeout_threshold_ns_per_insert = 80'000ULL; // 80 us
#else
        constexpr uint64_t timeout_threshold_ns_per_insert = 20'000ULL; // 20 us
#endif
        size_t insert_cnt_sum = 0;
        for (size_t t = 0; t < construct_time; ++t) {
            Table table;
            uint64_t temp_construct_ns = TestTableConstruct<true, false>
                    (table, src_vec);
            total_reserve_construct_ns += temp_construct_ns;
            insert_cnt_sum += element_num;
            uint64_t total_timeout_threshold_ns = timeout_threshold_ns_per_insert * insert_cnt_sum;
            if (total_reserve_construct_ns > total_timeout_threshold_ns) {
                fprintf(stderr, "Timeout in test insert with reserve, avg %.3f ns per insert in the initial test\n",
                        double(total_reserve_construct_ns) / double(insert_cnt_sum));
                total_reserve_construct_ns = 0;
                break;
            }
        }
    } catch(std::exception &e) {
        fprintf(stderr, "Catch exception when TestTableConstruct with reserve, element_num: %lu\n%s\n",
                element_num, e.what());
        total_reserve_construct_ns = 0;
    }

    // test insert without reserve
    size_t total_no_reserve_construct_ns = 0;
    try {
        constexpr uint64_t timeout_threshold_ns_per_insert = 20'000ULL; // 20 us
        size_t insert_cnt_sum = 0;
        for (size_t t = 0; t < construct_time; ++t) {
            Table table;
            uint64_t temp_construct_ns = TestTableConstruct<false, false>
                    (table, src_vec);
            total_no_reserve_construct_ns += temp_construct_ns;

            insert_cnt_sum += element_num;
            uint64_t total_timeout_threshold_ns = timeout_threshold_ns_per_insert * insert_cnt_sum;
            if (total_no_reserve_construct_ns > total_timeout_threshold_ns) {
                fprintf(stderr, "Timeout in test insert no reserve, avg %.3f ns per insert in the initial test\n",
                        double(total_no_reserve_construct_ns) / double(insert_cnt_sum));
                total_no_reserve_construct_ns = 0;
                break;
            }
        }
    }   catch(std::exception &e) {
        fprintf(stderr, "Catch exception when TestTableConstruct without reserve, element_num: %lu\n%s\n",
                element_num, e.what());
        total_no_reserve_construct_ns = 0;
    }

    if (total_reserve_construct_ns == 0 && total_no_reserve_construct_ns == 0) {
        fprintf(stderr, "%s with %s both construct timeout, skip all tests\n",
                MAP_NAME, HASH_NAME);
        return StatsTuple{0};
    }

    // generate a list of key not in the key set
    std::vector<mutable_value_type> lookup_vec;
    lookup_vec.reserve(src_vec.size());
    std::unique_ptr<KeyRNG> miss_key_rng_ptr;
    ConstructRngPtr<key_type, KeyRNG>(miss_key_rng_ptr, random_engine(), std::numeric_limits<size_t>::max());
    ConstructRngPtr<mapped_type, ValueRNG>(value_rng_ptr, random_engine(), std::numeric_limits<size_t>::max());
    ValueRandomGen miss_value_gen{random_engine(), std::move(*miss_key_rng_ptr), std::move(*value_rng_ptr)};
    for (size_t i = 0; i < src_vec.size(); ++i) {
        auto temp_pair = miss_value_gen();

        while (key_set.find(GetKey{}(temp_pair)) != key_set.end()) {
            temp_pair = miss_value_gen();
        }
        lookup_vec.push_back(temp_pair);
    }

    // test erase and insert
    uint64_t erase_and_insert_ns = 0;
    {
        if (timeout_flag_arr[check_timeout_index_arr[0]]) {
           fprintf(stderr, "%s Erase and insert test already timeout, not test\n", MAP_NAME);
        }
        else {
            Table table;
            std::tie(erase_and_insert_ns) = TestTableEraseAndInsert<Table>(miss_value_gen, src_vec,
                                                                           erase_time, seed);
        }

    }


    // test lookup keys in the map
    float no_rehash_load_factor = 0;
    uint64_t in_no_rehash_lookup_ns = 0;
    {
        if (timeout_flag_arr[check_timeout_index_arr[1]]) {
            fprintf(stderr, "%s hit no rehash find test already timeout, not test\n", MAP_NAME);
        }
        else {
            Table table;
            std::tie(in_no_rehash_lookup_ns, std::ignore) = TestTableLookUp<KEY_IN, false, Table,
                    std::vector<mutable_value_type>, GetKey>(table, lookup_time, src_vec,
                                                             src_vec, construct_seed, false);
            no_rehash_load_factor = table.load_factor();
        }
    }




    // test lookup keys not in the map
    uint64_t out_no_rehash_lookup_ns = 0;
    {
        if (timeout_flag_arr[check_timeout_index_arr[2]]) {
            fprintf(stderr, "%s miss no rehash find test already timeout, not test\n", MAP_NAME);
        }
        else {
            Table table;
            std::tie(out_no_rehash_lookup_ns, std::ignore) = TestTableLookUp<KEY_NOT_IN, false>(table, lookup_time, src_vec,
                                                                                                lookup_vec, construct_seed, false);
        }
    }

    // test look up keys, 50% of which are in the map
    // generate a vector of value_type contains 50% of the keys in the map
    std::vector<size_t> index_vec(element_num, 0);
    for (size_t i = 0; i < element_num; ++i) {
        index_vec[i] = i;
    }
    std::shuffle(index_vec.begin(), index_vec.end(), random_engine);
    std::vector<mutable_value_type> may_in_lookup_vec;
    may_in_lookup_vec.reserve(element_num);
    size_t half_element_num = element_num / 2UL;
    for (size_t i = 0; i < half_element_num; ++i) {
        may_in_lookup_vec.push_back(src_vec[index_vec[i]]);
    }
    std::shuffle(index_vec.begin(), index_vec.end(), random_engine);
    for (size_t i = half_element_num; i < element_num; ++i) {
        may_in_lookup_vec.push_back(lookup_vec[index_vec[i]]);
    }

    uint64_t may_no_hash_lookup_ns = 0;

    if (timeout_flag_arr[check_timeout_index_arr[3]]) {
        fprintf(stderr, "%s 50%% hit no rehash find test already timeout, not test\n", MAP_NAME);
    }
    else {
            Table table;
            std::tie(may_no_hash_lookup_ns, std::ignore) = TestTableLookUp<KEY_MAY_IN, false>(table, lookup_time, src_vec,
                                                                                              may_in_lookup_vec, construct_seed, false);
    }


    // test look up keys in the map with larger max_load_factor
    float with_rehash_load_factor = 0;
    uint64_t in_with_rehash_lookup_ns = 0, in_with_rehash_construct_ns = 0;
    {
        if (timeout_flag_arr[check_timeout_index_arr[4]]) {
            fprintf(stderr, "%s hit with rehash find test already timeout, not test\n", MAP_NAME);
        }
        else {
            Table table;
            std::tie(in_with_rehash_lookup_ns, in_with_rehash_construct_ns) = TestTableLookUp<KEY_IN, false, Table,
                    std::vector<mutable_value_type>, GetKey>(table, lookup_time, src_vec,
                                                             src_vec, construct_seed, true);
            with_rehash_load_factor = table.load_factor();
        }

    }

    // test lookup keys not in the map with larger max_load_factor
    uint64_t out_with_rehash_lookup_ns = 0, out_with_rehash_construct_ns = 0;
    {
        if (timeout_flag_arr[check_timeout_index_arr[5]]) {
            fprintf(stderr, "%s miss with rehash find test already timeout, not test\n", MAP_NAME);
        }
        else {
            Table table;
            std::tie(out_with_rehash_lookup_ns, out_with_rehash_construct_ns) = TestTableLookUp<KEY_NOT_IN, false>(table, lookup_time, src_vec,
                                                                                                                   lookup_vec, construct_seed, true);
        }

    }

    // test lookup keys 50% of which are in the map, with larger max_load_factor
    uint64_t may_with_rehash_lookup_ns = 0, may_with_rehash_construct_ns = 0;
    {
        if (timeout_flag_arr[check_timeout_index_arr[6]]) {
            fprintf(stderr, "%s 50%% hit with rehash find test already timeout, not test\n", MAP_NAME);
        }
        else {
            Table table;
            std::tie(may_with_rehash_lookup_ns, may_with_rehash_construct_ns) = TestTableLookUp<KEY_MAY_IN, false>(table, lookup_time, src_vec,
                                                                                                                   may_in_lookup_vec, construct_seed, true);
        }

    }

    // test iterate time
    uint64_t iterate_ns = 0, it_useless_sum = 0;
    uint64_t iterate_time = (lookup_time + element_num - 1) / element_num;
    {

        Table table;
        std::tie(iterate_ns, it_useless_sum) = TestTableIterate<Table,
                std::vector<mutable_value_type>, GetKey>(
                table, lookup_time / element_num, src_vec);
    }


    double avg_construct_time_with_reserve_ns = (double)total_reserve_construct_ns / (double)construct_time / (double)element_num;
    double avg_construct_time_without_reserve_ns = (double)total_no_reserve_construct_ns / (double)construct_time / (double)element_num;
    double avg_erase_insert_ns = (double)erase_and_insert_ns / (double)(erase_time * 2ULL);
    double avg_hit_without_rehash_lookup_ns = (double)in_no_rehash_lookup_ns / (double)lookup_time;
    double avg_miss_without_rehash_lookup_ns = (double)out_no_rehash_lookup_ns / (double)lookup_time;
    double avg_may_without_rehash_lookup_ns = (double)may_no_hash_lookup_ns / (double)lookup_time;
    double avg_hit_with_rehash_lookup_ns = (double)in_with_rehash_lookup_ns  / (double)lookup_time;
    double avg_miss_with_rehash_lookup_ns = (double)out_with_rehash_lookup_ns / (double)lookup_time;
    double avg_may_with_rehash_lookup_ns = (double)may_with_rehash_lookup_ns / (double)lookup_time;
    double avg_iterate_ns = (double)iterate_ns / double(iterate_time * element_num);
    double avg_final_rehash_construct_ns = (double)(in_with_rehash_construct_ns + out_with_rehash_construct_ns + may_with_rehash_construct_ns) / (double)(element_num * 3ULL);

    fprintf(stderr, "%s %lu elements, sizeof(pair)=%lu, insert with reserve avg use %.4f s,"
                    "insert no reserve avg use %.4f s, "
                    "erase and insert %.3f ns, "
                    "no rehash construct got load_factor: %.3f, "
                    "with rehash construct got load_factor: %.3f, "
                    "find hit no rehash use %.3f ns,"
                    "find miss no rehash use %.3f ns, "
                    "find 50%% hit no rehash use %.3f ns, "
                    "find hit with rehash use %.3f ns,"
                    "find miss with rehash use %.3f ns, "
                    "find 50%% hit with rehash use %.3f ns, "
                    "iterate use %.3f ns per, "
                    "with_final_rehash_insert: %.3f s\n",
                MAP_NAME, element_num, sizeof(value_type),
                (double)total_reserve_construct_ns / (1e+9) / (double)construct_time,
                (double)total_no_reserve_construct_ns / (1e+9) / (double)construct_time,
                (double)erase_and_insert_ns / (double)(erase_time * 2UL),
                no_rehash_load_factor, with_rehash_load_factor,
                (double)in_no_rehash_lookup_ns / (double)lookup_time,
                (double)out_no_rehash_lookup_ns / (double)lookup_time,
                (double)may_no_hash_lookup_ns / (double)lookup_time,
                (double)in_with_rehash_lookup_ns  / (double)lookup_time,
                (double)out_with_rehash_lookup_ns / (double)lookup_time,
                (double)may_with_rehash_lookup_ns / (double)lookup_time,
                (double)iterate_ns / double(iterate_time * element_num),
                double(in_with_rehash_construct_ns + out_with_rehash_construct_ns + may_with_rehash_construct_ns) / (1e+9) / 3.0
            );

    return {avg_construct_time_with_reserve_ns, avg_construct_time_without_reserve_ns,
            avg_erase_insert_ns,
            no_rehash_load_factor, with_rehash_load_factor,
            avg_hit_without_rehash_lookup_ns, avg_miss_without_rehash_lookup_ns, avg_may_without_rehash_lookup_ns,
            avg_hit_with_rehash_lookup_ns, avg_miss_with_rehash_lookup_ns, avg_may_with_rehash_lookup_ns,
            avg_iterate_ns, avg_final_rehash_construct_ns
            };


}

template<class T1, class T2, class T1RNG = fph::dynamic::RandomGenerator<T1>, class T2RNG = fph::dynamic::RandomGenerator<T2>>
class RandomPairGen {
public:
    RandomPairGen(size_t first_seed, T1RNG&& t1_rng, T2RNG&& t2_rng):
            init_seed(first_seed), t1_gen(std::move(t1_rng)), t2_gen(std::move(t2_rng)) {
        seed(first_seed);
    }

    std::pair<T1,T2> operator()() {
        return {t1_gen(), t2_gen()};
    }

    template<class SeedType>
    void seed(SeedType seed) {
        t1_gen.seed(seed);
        t2_gen.seed(seed);
    }

    size_t init_seed;

    using KeyRNGType = T1RNG;
    using ValueRNGType = T2RNG;

protected:
    T1RNG t1_gen;
    T2RNG t2_gen;
};

enum KeyBitsPattern{
    UNIFORM = 0,
    MASK_LOW_BITS,
    MASK_HIGH_BITS,
    MASK_SPLIT_BITS,
};


template<KeyBitsPattern key_pattern>
class MaskedUint64RNG {
public:

    MaskedUint64RNG(size_t seed, size_t max_element_size): init_seed(seed), random_engine(seed),
                        mask(0){
        size_t mask_len = fph::dynamic::detail::RoundUpLog2(max_element_size);
        if constexpr(key_pattern == MASK_LOW_BITS) {
            size_t low_mask = fph::dynamic::detail::GenBitMask<uint64_t>(std::numeric_limits<size_t>::digits - mask_len);
            mask = ~low_mask;
        }
        else if constexpr(key_pattern == MASK_HIGH_BITS) {
            mask = fph::dynamic::detail::GenBitMask<uint64_t>(mask_len);
        }
        else if constexpr(key_pattern == MASK_SPLIT_BITS) {
            mask = GenSplitMask(mask_len);
        }

    }

    MaskedUint64RNG(const MaskedUint64RNG& other): init_seed(other.init_seed),
                                                   random_engine(other.random_engine),
                                                   random_gen(other.random_gen),
                                                   mask(other.mask) {}

    MaskedUint64RNG(MaskedUint64RNG&& other) noexcept:
            init_seed(std::exchange(other.init_seed, 0)),
            random_engine(std::move(other.random_engine)),
            random_gen(std::move(other.random_gen)),
            mask(std::exchange(other.mask, 0)){}

    MaskedUint64RNG& operator=(MaskedUint64RNG&& other) noexcept {
        this->init_seed = std::exchange(other.init_seed, 0);
        this->random_engine = std::move(other.random_engine);
        this->random_gen = std::move(other.random_gen);
        this->mask = std::exchange(other.mask, 0);
        return *this;
    }

    uint64_t operator()() {
        auto ret = random_gen(random_engine);

        if constexpr(key_pattern == MASK_HIGH_BITS || key_pattern == MASK_LOW_BITS || key_pattern == MASK_SPLIT_BITS) {
            ret &= mask;
        }
        return ret;
    }

    void seed(uint64_t seed = 0) {
        init_seed = seed;
        random_engine.seed(seed);
    }

    size_t init_seed;

protected:
    std::mt19937_64 random_engine;
    std::uniform_int_distribution<uint64_t> random_gen;
    uint64_t mask;

    static size_t GenSplitMask(size_t need_digits) {
        constexpr size_t full_digits = std::numeric_limits<size_t>::digits;
        size_t padding_time = need_digits > 0UL ? need_digits - 1UL : 0UL;
        size_t padding_digits = full_digits - need_digits;
        size_t per_padding_digits = padding_digits  / padding_time;
        size_t has_padding_digits = 0UL;
        size_t mask = need_digits > 0UL ? 1UL : 0UL;
        for (size_t i = 0; i < padding_time; ++i) {
            size_t this_padding_digits = (i + 1UL == padding_time ? (padding_digits - has_padding_digits) : per_padding_digits);
            mask = (mask << (this_padding_digits + 1UL)) | 1UL;
            has_padding_digits += this_padding_digits;
        }
        return mask;
    }

};

template<size_t max_len, bool fix_length = false>
class StringRNG {
public:

//    StringRNG(): init_seed(std::random_device{}()), random_engine(init_seed) {}
    StringRNG(const StringRNG& other): init_seed(other.init_seed),
                                                   random_engine(other.random_engine) {}

    explicit StringRNG(size_t seed): init_seed(seed), random_engine(seed) {}

    StringRNG(StringRNG&& other) noexcept:
            init_seed(std::exchange(other.init_seed, 0)),
            random_engine(std::move(other.random_engine)){}

    StringRNG& operator=(StringRNG&& other) noexcept {
        this->init_seed = std::exchange(other.init_seed, 0);
        this->random_engine = std::move(other.random_engine);
        return *this;
    }

    std::string operator()(size_t length) {
        static constexpr char alphanum[] =
                "0123456789"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "abcdefghijklmnopqrstuvwxyz";
        std::string ret(length, 0);
        for (size_t i = 0; i < length; ++i) {
            ret[i] = alphanum[random_gen(random_engine) % (sizeof(alphanum) - 1)];
        }
        return ret;
    }

    template<size_t length>
    std::string RandomGenStr() {
        static constexpr char alphanum[] =
                "0123456789"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "abcdefghijklmnopqrstuvwxyz";
        std::string ret(length, 0);
        for (size_t i = 0; i < length; ++i) {
            ret[i] = alphanum[random_gen(random_engine) % (sizeof(alphanum) - 1)];
        }
        return ret;
    }


    /**
     * random generate a string consists of alpha num with random length from [1, 10]
     * @return
     */
    std::string operator()() {
        // TODO: change default size to bigger number if needed

        if constexpr (fix_length) {
            return RandomGenStr<max_len>();
        }
        else {
            size_t random_len = 1UL + random_gen(random_engine) % max_len;
            return (*this)(random_len);
        }
    }

    void seed(uint64_t seed = 0) {
        init_seed = seed;
        random_engine.seed(seed);
    }

    size_t init_seed;

protected:
    std::minstd_rand random_engine;
    std::uniform_int_distribution<uint32_t> random_gen;
};

template<size_t size>
struct FixSizeStruct {
    constexpr FixSizeStruct()noexcept: data{0} {}
    char data[size];

    friend bool operator==(const FixSizeStruct<size>&a, const FixSizeStruct<size>&b) {
        return memcmp(a.data, b.data, size) == 0;
    }
};

template<size_t size>
class FixSizeStructRNG {
public:
    FixSizeStructRNG(): init_seed(std::random_device{}()), uint_gen(init_seed) {};
    FixSizeStructRNG(size_t seed): init_seed(seed), uint_gen(seed) {}

    FixSizeStruct<size> operator()() {
        FixSizeStruct<size> ret{};
        *reinterpret_cast<uint8_t*>(&ret) = (uint8_t)uint_gen();
        return ret;
    }

    void seed(size_t seed) {
        init_seed = seed;
        uint_gen.seed(seed);
    }

    size_t init_seed;

protected:
    fph::dynamic::RandomGenerator<uint64_t> uint_gen;
};

constexpr char PathSeparator() {
#ifdef BENCH_OS_WIN
    return '\\';
#else
    return '/';
#endif
}

template<class Table1, class Table2, class GetKey = SimpleGetKey<typename Table1::value_type>,
        class ValueEqual = std::equal_to<typename Table1::value_type>>
bool IsTableSame(const Table1 &table1, const Table2 &table2) {
    if (table1.size() != table2.size()) {
        return false;
    }
    size_t table_size = table2.size();
    size_t element_cnt = 0;
    for (const auto& pair :table1) {
        ++element_cnt;
        auto find_it = table2.find(GetKey{}(pair));
        if FPH_UNLIKELY(find_it == table2.end()) {
            fprintf(stderr, "Fail to find %s in table2, can find in table1 status: %d", ToString(GetKey{}(pair)).c_str(), table1.find(GetKey{}(pair)) != table1.end());
            return false;
        }
        if FPH_UNLIKELY(!ValueEqual{}(pair, *find_it)) {
            return false;
        }
    }
    if FPH_UNLIKELY(element_cnt != table_size) {
        fprintf(stderr, "Table 1 iterate num not equals to table size");
        return false;
    }
    element_cnt = 0;
    for (const auto& pair :table2) {
        ++element_cnt;
        auto find_it = table1.find(GetKey{}(pair));
        if FPH_UNLIKELY(find_it == table1.end()) {
            fprintf(stderr, "Fail to find %s in table1", ToString(GetKey{}(pair)).c_str());
            return false;
        }
        if FPH_UNLIKELY(!ValueEqual{}(pair, *find_it)) {
            return false;
        }
    }
    if FPH_UNLIKELY(element_cnt != table_size) {
        fprintf(stderr, "Table 2 iterate num not equals to table size");
        return false;
    }
    return true;
}

template<class Table, class ValueRandomGen, class value_type>
int TestBasicCorrect(size_t seed) {
    using key_type = typename Table::key_type;
    using mapped_type = typename Table::mapped_type;

    using KeyRNG = typename ValueRandomGen::KeyRNGType;
    using ValueRNG = typename ValueRandomGen::ValueRNGType;

    std::unique_ptr<KeyRNG> key_rng_ptr;
    std::unique_ptr<ValueRNG> value_rng_ptr;

    {
        Table table;
        std::unordered_map<typename value_type::first_type, typename value_type::second_type> bench_table;
        const size_t element_num = 100;
        ConstructRngPtr<key_type, KeyRNG>(key_rng_ptr, seed, element_num);
        ConstructRngPtr<mapped_type, ValueRNG>(value_rng_ptr, seed, element_num);
        ValueRandomGen value_gen{seed, std::move(*key_rng_ptr), std::move(*value_rng_ptr)};
        value_gen.seed(seed);
        std::vector<value_type> src_vec;
        for (size_t i = 0; i < element_num; ++i) {
            src_vec.template emplace_back(value_gen());
        }
        try {

            for (size_t i = 0; i < element_num; ++i) {
                const auto& pair = src_vec[i];
                table.insert(pair);
                bench_table.insert(pair);
                if (!IsTableSame(table, bench_table)) {
                    fprintf(stderr, "%s with %s table not same with std during the insert test\n",
                            MAP_NAME, HASH_NAME);
                    return -1;
                }
            }
            if (!IsTableSame(table, bench_table)) {
                fprintf(stderr, "%s with %s Failed to Pass the insert test\n", MAP_NAME, HASH_NAME);
                return -1;
            }
        }
        catch(std::exception &e) {
            fprintf(stderr, "Catch exception when test insert operations %s with %s\n%s\n",
                    MAP_NAME, HASH_NAME, e.what());
            return -1;
        }

    }
    return 0;
}

template<class KeyType, class ValueType, class KeyRandomGen, class ValueRandomGen>
auto TestOnePairType( size_t seed, const std::vector<size_t>& key_size_array) {

    using RandomGenerator = RandomPairGen<KeyType, ValueType, KeyRandomGen , ValueRandomGen>;

    std::mt19937_64 uint64_rng{seed};

    using PairType = std::pair<KeyType, ValueType>;
    using TestPerformanceMap = Map<KeyType, ValueType>;

    static_assert(is_pair<typename TestPerformanceMap::value_type>::value);

    fprintf(stderr, "sizeof(%s) with %s is %zu\n\n", MAP_NAME, HASH_NAME, sizeof(TestPerformanceMap));


    if (TestBasicCorrect<TestPerformanceMap, RandomGenerator, PairType>(uint64_rng()) != 0) {
        fprintf(stderr, "%s with %s failed to pass the basic correctness test\n", MAP_NAME, HASH_NAME);
        return std::vector(key_size_array.size(), StatsTuple({0}));
    }


    TimeoutFlagArr timeout_flag_arr{};
    for (auto &timeout_flag: timeout_flag_arr) {
        timeout_flag = false;
    }
    std::vector<StatsTuple> result_vec;
    bool already_time_out_flag = false;
    for (auto key_num: key_size_array) {
        size_t LOOKUP_TIME = 20'000'000ULL;
        size_t ERASE_TIME = 10'000'000ULL;
        size_t CONSTRUCT_TIME = 2;
        if (key_num < 1000UL) {
            CONSTRUCT_TIME = 100;
        }
        if (key_num < 100000UL) {
            CONSTRUCT_TIME = 6;
        }
        else if (key_num < 1000000UL) {
            CONSTRUCT_TIME = 3;
        }
        else if (key_num < 10000000UL) {
            CONSTRUCT_TIME = 2;
        }

        if (already_time_out_flag) {
            fprintf(stderr, "%s with %s Already timeout for all lookups and erase test, not test for element size: %lu\n",
                    MAP_NAME, HASH_NAME, key_num);
            result_vec.template emplace_back(StatsTuple{0});
            continue;
        }
        StatsTuple stats_tuple = TestTablePerformance<RandomGenerator, TestPerformanceMap, PairType>(
                key_num, CONSTRUCT_TIME, LOOKUP_TIME, ERASE_TIME, timeout_flag_arr, uint64_rng());
        result_vec.push_back(stats_tuple);
        already_time_out_flag = true;
        for (size_t check_timeout_index: check_timeout_index_arr) {
            if (stats_tuple[check_timeout_index] != 0.0) {
                already_time_out_flag = false;
            }
            else {
                timeout_flag_arr[check_timeout_index] = true;
            }
        }

    }
    return result_vec;




}

//void TestRNG() {
//    using MaskHighBitsUint64RNG = MaskedUint64RNG<MASK_HIGH_BITS>;
//    using MaskLowBitsUint64RNG = MaskedUint64RNG<MASK_LOW_BITS>;
//    using MaskSplitBitsUint64RNG = MaskedUint64RNG<MASK_SPLIT_BITS>;
////    using UniformUint64RNG = MaskedUint64RNG<UNIFORM>;
//
//    fprintf(stderr, "Mask low bits 16 bit\n");
//    MaskLowBitsUint64RNG low_rng_4_bit_1(0, (1UL << 16) - 4);
//    for (int i = 0; i < 32; ++i) {
//        fprintf(stderr, "%lx, ", low_rng_4_bit_1());
//    }
//    fprintf(stderr, "\n");
//
//    MaskHighBitsUint64RNG high_rng_8bit_1(0, 256);
//    fprintf(stderr, "Mask High bits 8 bit\n");
//    for (int i = 0; i < 32; ++i) {
//        fprintf(stderr, "%lx, ", high_rng_8bit_1());
//    }
//    fprintf(stderr, "\n");
//
//    MaskSplitBitsUint64RNG split_rng(0, 100000000ULL);
//    fprintf(stderr, "Mask mid split bits\n");
//    for (int i = 0; i < 32; ++i) {
//        fprintf(stderr, "%lx, ", split_rng());
//    }
//    fprintf(stderr, "\n");
//
//}

void ExportToCsv(FILE* export_fp, const std::vector<size_t>& element_num_vec, const std::vector<StatsTuple>& result_vec) {
    const char* csv_header = "element_num,avg_construct_time_with_reserve_ns,avg_construct_time_without_reserve_ns,"
                             "avg_erase_insert_ns,"
                             "no_rehash_load_factor,with_rehash_load_factor,"
                             "avg_hit_without_rehash_lookup_ns,avg_miss_without_rehash_lookup_ns,"
                             "avg_50%_hit_without_rehash_lookup_ns,"
                             "avg_hit_with_rehash_lookup_ns,avg_miss_with_rehash_lookup_ns,"
                             "avg_50%_hit_with_rehash_lookup_ns,"
                             "avg_iterate_ns,avg_with_final_rehash_construct_ns";
    fprintf(export_fp, "%s\n", csv_header);
    if (element_num_vec.size() != result_vec.size()) {
        fprintf(stderr, "Error, element num vec size != result vec size");
        std::abort();
    }
    for (size_t k = 0; k < element_num_vec.size(); ++k) {
        size_t element_num = element_num_vec[k];
        fprintf(export_fp, "%lu,", element_num);
        const auto& stats_tuple = result_vec[k];
        for (size_t i = 0; i < std::tuple_size_v<StatsTuple>; ++i) {
            fprintf(export_fp, "%f", stats_tuple[i]);
            if (i + 1UL != std::tuple_size_v<StatsTuple>) {
                fprintf(export_fp, ",");
            }
            else fprintf(export_fp, "\n");
        }
    }
    fclose(export_fp);
}


void BenchTest(size_t seed, const char* data_dir) {
//    TestRNG();
    using UniformUint64RNG = MaskedUint64RNG<UNIFORM>;
#ifndef BENCH_ONLY_STRING
    using MaskHighBitsUint64RNG = MaskedUint64RNG<MASK_HIGH_BITS>;
    using MaskLowBitsUint64RNG = MaskedUint64RNG<MASK_LOW_BITS>;
    using MaskSplitBitsUint64RNG = MaskedUint64RNG<MASK_SPLIT_BITS>;

#endif


    std::string map_name = std::string(MAP_NAME);
    std::string hash_name = std::string(HASH_NAME);
    std::string data_dir_path = std::string(data_dir) + PathSeparator();




    std::vector<size_t> key_size_array = {
                                    32UL, 110UL, 240UL, 500UL, 800UL, 1024UL, 1500UL,
                                    2048UL, 3000UL, 6000UL,
                                            8192UL, 12000UL,16384UL, 25000UL,
                                          32768UL, 45000UL, 60000UL,
                                          100000UL, 150000UL, 200000UL, 300000UL, 400000UL, 600000UL,
                                          800000UL, 1200000UL,
                                          2200000UL, 3100000UL, 6000000UL,
                                          10000000UL};

    fprintf(stderr, "\n------ Begin to test hash %s with map %s ---\n", HASH_NAME, MAP_NAME);

    {

#ifndef BENCH_ONLY_STRING



        {
            fprintf(stderr, "\nTest Mid split bits masked distributed uint64 key\n\n");
            std::string data_file_name =
                    map_name + "__" + hash_name + "__" + "mask_split_bits_uint64_t" + "__" + "uint64_t" +
                    ".csv";
            std::string export_file_path = data_dir_path + data_file_name;
            FILE *export_fp = fopen(export_file_path.c_str(), "w");
            if (export_fp == nullptr) {
                fprintf(stderr, "Error when create file at %s\n%s", export_file_path.c_str(),
                        std::strerror(errno));
                return;
            }
            auto result_vec = TestOnePairType<uint64_t, uint64_t, MaskSplitBitsUint64RNG, UniformUint64RNG>(
                    seed, key_size_array);
            ExportToCsv(export_fp, key_size_array, result_vec);
        }

        {
            fprintf(stderr, "\nTest Uniformly distributed uint64 key\n\n");
            std::string data_file_name =
                    map_name + "__" + hash_name + "__" + "uniform_uint64_t" + "__" + "uint64_t" +
                    ".csv";
            std::string export_file_path = data_dir_path + data_file_name;
            FILE *export_fp = fopen(export_file_path.c_str(), "w");
            if (export_fp == nullptr) {
                fprintf(stderr, "Error when create file at %s\n%s", export_file_path.c_str(),
                        std::strerror(errno));
                return;
            }
            auto result_vec = TestOnePairType<uint64_t, uint64_t, UniformUint64RNG, UniformUint64RNG>(
                    seed, key_size_array);
            ExportToCsv(export_fp, key_size_array, result_vec);
        }

        {
            fprintf(stderr, "\nTest High bits masked uint64 key\n\n");
            std::string data_file_name =
                    map_name + "__" + hash_name + "__" + "mask_high_bits_uint64_t" + "__" +
                    "uint64_t" + ".csv";
            std::string export_file_path = data_dir_path + data_file_name;
            FILE *export_fp = fopen(export_file_path.c_str(), "w");
            if (export_fp == nullptr) {
                fprintf(stderr, "Error when create file at %s\n%s", export_file_path.c_str(),
                        std::strerror(errno));
                return;
            }
            auto result_vec = TestOnePairType<uint64_t, uint64_t, MaskHighBitsUint64RNG, UniformUint64RNG>(
                    seed, key_size_array);
            ExportToCsv(export_fp, key_size_array, result_vec);
        }

        {

            fprintf(stderr, "\nTest Low bits masked uint64 key\n\n");
            std::string data_file_name =
                    map_name + "__" + hash_name + "__" + "mask_low_bits_uint64_t" + "__" +
                    "uint64_t" + ".csv";
            std::string export_file_path = data_dir_path + data_file_name;
            FILE *export_fp = fopen(export_file_path.c_str(), "w");
            if (export_fp == nullptr) {
                fprintf(stderr, "Error when create file at %s\n%s\n", export_file_path.c_str(),
                        std::strerror(errno));
                return;
            }
            auto result_vec = TestOnePairType<uint64_t, uint64_t, MaskLowBitsUint64RNG, UniformUint64RNG>(
                    seed, key_size_array);
            ExportToCsv(export_fp, key_size_array, result_vec);
        }

        {
            fprintf(stderr, "\nTest mid split bits masked distributed uint64 key and 56 bytes payload\n\n");
            std::string data_file_name =
                    map_name + "__" + hash_name + "__" + "mask_split_bits_uint64_t" + "__" +
                    "56bytes_payload" + ".csv";
            std::string export_file_path = data_dir_path + data_file_name;
            FILE *export_fp = fopen(export_file_path.c_str(), "w");
            if (export_fp == nullptr) {
                fprintf(stderr, "Error when create file at %s\n%s", export_file_path.c_str(),
                        std::strerror(errno));
                return;
            }
            auto result_vec = TestOnePairType<uint64_t, FixSizeStruct<56>, MaskSplitBitsUint64RNG, FixSizeStructRNG<56>>(
                    seed, key_size_array);
            ExportToCsv(export_fp, key_size_array, result_vec);
        }

#endif

#ifndef BENCH_ONLY_INT

        {
            using BigStringRNG = StringRNG<128, true>;
            fprintf(stderr, "\nTest Long Len String with fixed length 128\n\n");
            std::string data_file_name =
                    map_name + "__" + hash_name + "__" + "mid_string_fix_128" + "__" + "uint64_t" +
                    ".csv";
            std::string export_file_path = data_dir_path + data_file_name;
            FILE *export_fp = fopen(export_file_path.c_str(), "w");
            if (export_fp == nullptr) {
                fprintf(stderr, "Error when create file at %s\n%s", export_file_path.c_str(),
                        std::strerror(errno));
                return;
            }
            auto result_vec = TestOnePairType<std::string, uint64_t, BigStringRNG, UniformUint64RNG>(
                    seed, key_size_array);
            ExportToCsv(export_fp, key_size_array, result_vec);
        }

        {
            fprintf(stderr, "\nTest Small Random Len String with max length 12\n\n");
            using SmallStringRNG = StringRNG<12, false>;
            std::string data_file_name =
                    map_name + "__" + hash_name + "__" + "small_string_max_12" + "__" + "uint64_t" +
                    ".csv";
            std::string export_file_path = data_dir_path + data_file_name;
            FILE *export_fp = fopen(export_file_path.c_str(), "w");
            if (export_fp == nullptr) {
                fprintf(stderr, "Error when create file at %s\n%s", export_file_path.c_str(),
                        std::strerror(errno));
                return;
            }
            auto result_vec = TestOnePairType<std::string, uint64_t, SmallStringRNG, UniformUint64RNG>(
                    seed, key_size_array);
            ExportToCsv(export_fp, key_size_array, result_vec);
        }

        {
            fprintf(stderr, "\nTest Small String with fixed length 12\n\n");
            using SmallStringRNG = StringRNG<12, true>;
            std::string data_file_name =
                    map_name + "__" + hash_name + "__" + "small_string_fix_12" + "__" + "uint64_t" +
                    ".csv";
            std::string export_file_path = data_dir_path + data_file_name;
            FILE *export_fp = fopen(export_file_path.c_str(), "w");
            if (export_fp == nullptr) {
                fprintf(stderr, "Error when create file at %s\n%s", export_file_path.c_str(),
                        std::strerror(errno));
                return;
            }
            auto result_vec = TestOnePairType<std::string, uint64_t, SmallStringRNG, UniformUint64RNG>(
                    seed, key_size_array);
            ExportToCsv(export_fp, key_size_array, result_vec);
        }

        {
            using MidStringRNG = StringRNG<56, false>;
            fprintf(stderr, "\nTest Mid Random Len String with max length 56\n\n");
            std::string data_file_name =
                    map_name + "__" + hash_name + "__" + "mid_string_max_56" + "__" + "uint64_t" +
                    ".csv";
            std::string export_file_path = data_dir_path + data_file_name;
            FILE *export_fp = fopen(export_file_path.c_str(), "w");
            if (export_fp == nullptr) {
                fprintf(stderr, "Error when create file at %s\n%s", export_file_path.c_str(),
                        std::strerror(errno));
                return;
            }
            auto result_vec = TestOnePairType<std::string, uint64_t, MidStringRNG, UniformUint64RNG>(
                    seed, key_size_array);
            ExportToCsv(export_fp, key_size_array, result_vec);
        }

        {
            using MidStringRNG = StringRNG<56, true>;
            fprintf(stderr, "\nTest Mid Len String with fixed length 56\n\n");
            std::string data_file_name =
                    map_name + "__" + hash_name + "__" + "mid_string_fix_56" + "__" + "uint64_t" +
                    ".csv";
            std::string export_file_path = data_dir_path + data_file_name;
            FILE *export_fp = fopen(export_file_path.c_str(), "w");
            if (export_fp == nullptr) {
                fprintf(stderr, "Error when create file at %s\n%s", export_file_path.c_str(),
                        std::strerror(errno));
                return;
            }
            auto result_vec = TestOnePairType<std::string, uint64_t, MidStringRNG, UniformUint64RNG>(
                    seed, key_size_array);
            ExportToCsv(export_fp, key_size_array, result_vec);
        }


#endif
    }



}

int main(int argc, const char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Invalid parameters!\nUsage: bench_{map_name}__{hash_name} seed(size_t) export_data_dir");
        return -1;
    }
    size_t seed = std::stoul(std::string(argv[1]));
    BenchTest(seed, argv[2]);
    return 0;
}
