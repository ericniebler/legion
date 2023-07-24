/* Copyright 2023 NVIDIA Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "helpers.h"
#include <legion/legion_c.h>

enum IDs
{
  TASK_TOP_LEVEL,
  TASK_MAKE_INTEGER,
  REDOP_INTEGER_ADD,
  REDOP_STRING_CONCAT,
  SHARDING_FN_CONTIG,
};

class RedopIntegerAdd
{
public:
  typedef int RHS;
  typedef int LHS;

  template<bool EXCL>
  void apply(LHS &acc, RHS cur) const
  {
    acc += cur;
  }

  static const RHS identity;

  template<bool EXCL>
  void fold(RHS &a, RHS b) const
  {
    a += b;
  }
};

const RedopIntegerAdd::RHS RedopIntegerAdd::identity = 0;

class RedopStringConcat
{
public:
  typedef char *RHS;
  typedef char *LHS;
  static const RHS identity;

  template<bool EXCL>
  void fold(RHS &a, RHS b) const
  {
    abort();
  }

  template<bool EXCL>
  void apply(LHS &acc, RHS cur) const
  {
    abort();
  }

  static void serdez_init(const ReductionOp *op, void *&out, size_t &size_out)
  {
    out = strdup("");
    size_out = 1;
  }

  static void serdez_fold(const ReductionOp *op,
                          void *&inout,
                          size_t &size_out,
                          const void *other)
  {
    std::string out = std::string((const char *)inout) +
                      std::string((const char *)other);

    free(inout);
    inout = strdup(out.c_str());
    size_out = out.size() + 1;
  }
};

const RedopStringConcat::RHS RedopStringConcat::identity = NULL;

class ContigShardingFunctor: public ShardingFunctor
{
public:
  virtual ShardID shard(const DomainPoint &index_point,
                        const Domain &index_domain,
                        const size_t total_shards)
  {
    assert(index_domain.lo().point_data[0] == 0);

    return (total_shards * index_point.point_data[0]) /
           (index_domain.hi().point_data[0] + 1);
  }
};

int task_make_int(const Task *task,
                  const std::vector<PhysicalRegion> &regions,
                  Context ctx,
                  Runtime *runtime)
{
  return task->index_point[0] + 1;
}

UntypedBuffer make_string(int i)
{
  int count = i + 1;
  char *str = strdup(std::string(count, '0' + count).c_str());
  return UntypedBuffer(str, count + 1);
}

struct Reduction
{
  // in
  Context ctx;
  Runtime *rt;
  ReductionOpID redop_id;
  FutureMap futures;

  // out
  Future result;
  size_t result_size;
  const void *result_buffer;

  Reduction(Context ctx,
            Runtime *rt,
            ReductionOpID redop_id,
            FutureMap futures)
    :ctx(ctx), rt(rt), redop_id(redop_id), futures(futures)
  {
  }

  void run(Future initial)
  {
    result = rt->reduce_future_map(ctx,
                                   futures,
                                   redop_id,
                                   true,
                                   0,
                                   0,
                                   NULL,
                                   initial);
    result_buffer = result.get_buffer(Memory::SYSTEM_MEM, &result_size);
  }

  void run_capi(Future initial)
  {
    legion_runtime_t rt_c = {&rt};
    legion_context_t ctx_c = {&ctx};
    legion_future_map_t futures_c = {&futures};
    legion_future_t initial_c = {&initial};

    legion_future_t result_c = legion_future_map_reduce_with_initial_value(
        rt_c, ctx_c, futures_c, redop_id, true, 0, 0, NULL, initial_c);

    result = *(Future *)result_c.impl;
    legion_future_destroy(result_c);
    result_buffer = result.get_buffer(Memory::SYSTEM_MEM, &result_size);
  }

  bool is_expected(const void *expected_value, size_t expected_size)
  {
    return result_size == expected_size &&
           memcmp(result_buffer, expected_value, expected_size) == 0;
  }

  bool is_expected_string(const char *str)
  {
    return is_expected(str, strlen(str) + 1);
  }

  bool is_expected_integer(int i)
  {
    return is_expected(&i, sizeof i);
  }

  const char *as_string()
  {
    return (const char *)result_buffer;
  }

  int as_integer()
  {
    return *(int *)result_buffer;
  }
};

int expected_integer(size_t count)
{
  int sum = 0;
  for (unsigned i = 1; i <= count; i++)
    sum += i;
  return sum;
}

std::string expected_string(size_t count)
{
  std::string out = "";
  for (unsigned i = 0; i < count; i++)
    out += std::string(i + 1, '0' + i + 1);
  return out;
}

void do_integer_add_test(Context ctx, Runtime *rt)
{
  size_t task_count = 8;
  IndexTaskLauncher launcher(TASK_MAKE_INTEGER,
                             Rect<1>(0, task_count - 1),
                             UntypedBuffer(),
                             ArgumentMap());

  FutureMap futures = rt->execute_index_space(ctx, launcher);

  Reduction reduction(ctx,
                      rt,
                      REDOP_INTEGER_ADD,
                      rt->execute_index_space(ctx, launcher));

  reduction.run(Future());
  printf("%d\n", reduction.as_integer());
  assert(reduction.is_expected_integer(expected_integer(task_count)));

  int x = 1000;
  reduction.run_capi(Future::from_value(x));
  printf("%d\n", reduction.as_integer());
  assert(reduction.is_expected_integer(x + expected_integer(task_count)));
}

void do_string_concat_test(Context ctx, Runtime *rt)
{
  std::map<DomainPoint, UntypedBuffer> map_data;

  int future_count = 8;
  DomainPoint point(0);
  point.dim = 1;

  int shard_id = rt->get_shard_id(ctx, true);
  int shard_count = rt->get_num_shards(ctx, true);

  assert(future_count % shard_count == 0);
  int points_per_shard = future_count / shard_count;

  for (int i = 0; i < points_per_shard; i++)
  {
    int idx = i + points_per_shard * shard_id;
    point.point_data[0] = idx;
    map_data[point] = make_string(idx);
  }

  Rect<1> rect(0, future_count - 1);
  IndexSpace is = rt->create_index_space(ctx, rect);

  Reduction reduction(ctx,
                      rt,
                      REDOP_STRING_CONCAT,
                      rt->construct_future_map(ctx,
                                               is,
                                               map_data,
                                               /* collective = */ true,
                                               SHARDING_FN_CONTIG));

  std::string expected = expected_string(future_count);

  reduction.run(Future());
  printf("%s\n", reduction.as_string());
  assert(reduction.is_expected_string(expected.c_str()));

  char init[] = "init";
  reduction.run(Future::from_untyped_pointer(init, strlen(init) + 1));
  printf("%s\n", reduction.as_string());

  expected = "init" + expected;
  assert(reduction.is_expected_string(expected.c_str()));
}

void task_top_level(const Task *task,
                    const std::vector<PhysicalRegion> &regions,
                    Context ctx,
                    Runtime *rt)
{
  do_integer_add_test(ctx, rt);
  do_string_concat_test(ctx, rt);
}

int main(int argc, char **argv)
{
  Runtime::register_reduction_op<RedopIntegerAdd>(REDOP_INTEGER_ADD);

  Runtime::preregister_sharding_functor(SHARDING_FN_CONTIG,
                                        new ContigShardingFunctor());

  ReductionOp *cat = ReductionOp::create_reduction_op<RedopStringConcat>();
  Runtime::register_reduction_op(REDOP_STRING_CONCAT,
                                 cat,
                                 RedopStringConcat::serdez_init,
                                 RedopStringConcat::serdez_fold,
                                 false);

  regLocTask<int, task_make_int>("task_make_integer",
                                 TASK_MAKE_INTEGER);

  regLocTask<task_top_level>("top_level", TASK_TOP_LEVEL);
  Runtime::set_top_level_task_id(TASK_TOP_LEVEL);
  return Runtime::start(argc, argv);
}
