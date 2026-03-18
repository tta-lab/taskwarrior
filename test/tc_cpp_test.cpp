////////////////////////////////////////////////////////////////////////////////
//
// Copyright 2022, Dustin J. Mitchell
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// https://www.opensource.org/licenses/mit-license.php
//
////////////////////////////////////////////////////////////////////////////////

#include <cmake.h>
// cmake.h include header must come first

#include <rust/cxx.h>
#include <stdlib.h>
#include <taskchampion-cpp/lib.h>
#include <test.h>

std::string uuid2str(tc::Uuid uuid) { return static_cast<std::string>(uuid.to_string()); }

////////////////////////////////////////////////////////////////////////////////
// Tests for the basic cxxbridge functionality. This focuses on the methods with
// complex cxxbridge implementations, rather than those with complex Rust
// implementations but simple APIs, like sync.
int TEST_NAME(int, char**) {
  UnitTest t;
  std::string str;

  auto replica = tc::new_replica_for_test();
  auto uuid = tc::uuid_v4();
  auto uuid2 = tc::uuid_v4();
  t.is(uuid2str(uuid).size(), (size_t)36, "uuid string is the right length");

  rust::Vec<tc::Operation> ops;
  auto task = tc::create_task(uuid, ops);
  t.is(uuid2str(task->get_uuid()), uuid2str(uuid), "new task has correct uuid");
  task->update("status", "pending", ops);
  task->update("description", "a task", ops);
  task->update("description", "a cool task", ops);

  t.is(ops[0].is_create(), true, "ops[0] is create");
  t.is(uuid2str(ops[0].get_uuid()), uuid2str(uuid), "ops[0] has correct uuid");

  t.is(ops[1].is_update(), true, "ops[1] is update");
  t.is(uuid2str(ops[1].get_uuid()), uuid2str(uuid), "ops[1] has correct uuid");
  ops[1].get_property(str);
  t.is(str, "status", "ops[1] property is 'status'");
  t.ok(ops[1].get_value(str), "get_value succeeds");
  t.is(str, "pending", "ops[1] value is 'pending'");
  t.ok(!ops[1].get_old_value(str), "get_old_value has no old value");

  t.is(ops[2].is_update(), true, "ops[2] is update");
  t.is(uuid2str(ops[2].get_uuid()), uuid2str(uuid), "ops[2] has correct uuid");
  ops[2].get_property(str);
  t.is(str, "description", "ops[2] property is 'description'");
  t.ok(ops[2].get_value(str), "get_value succeeds");
  t.is(str, "a task", "ops[2] value is 'a task'");
  t.ok(!ops[2].get_old_value(str), "get_old_value has no old value");

  t.is(ops[3].is_update(), true, "ops[3] is update");
  t.is(uuid2str(ops[3].get_uuid()), uuid2str(uuid), "ops[3] has correct uuid");
  ops[3].get_property(str);
  t.is(str, "description", "ops[3] property is 'description'");
  t.ok(ops[3].get_value(str), "get_value succeeds");
  t.is(str, "a cool task", "ops[3] value is 'a cool task'");
  t.ok(ops[3].get_old_value(str), "get_old_value succeeds");
  t.is(str, "a task", "ops[3] old value is 'a task'");

  replica->commit_operations(std::move(ops));
  auto maybe_task2 = replica->get_task_data(tc::uuid_v4());
  t.ok(maybe_task2.is_none(), "looking up a random uuid gets nothing");

  // PowerSync does not track undo operations locally (sync is external).
  // Verify get_undo_operations and commit_reversed_operations succeed without error.
  auto undo_ops = replica->get_undo_operations();
  replica->commit_reversed_operations(std::move(undo_ops));

  auto maybe_task3 = replica->get_task_data(uuid);
  t.ok(maybe_task3.is_some(), "looking up the original uuid gets TaskData");
  rust::Box<tc::TaskData> task3 = maybe_task3.take();
  t.is(uuid2str(task3->get_uuid()), uuid2str(uuid), "reloaded task has correct uuid");
  t.ok(task3->get("description", str), "reloaded task has a description");
  t.is(str, "a cool task", "reloaded task has correct description");
  t.ok(task3->get("status", str), "reloaded task has a status");
  t.is(str, "pending", "reloaded task has correct status");

  t.is(task3->properties().size(), (size_t)2, "task has 2 properties");
  t.is(task3->items().size(), (size_t)2, "task has 2 items");

  rust::Vec<tc::Operation> ops2;
  auto task4 = tc::create_task(uuid2, ops2);
  task4->update("description", "another", ops2);
  replica->commit_operations(std::move(ops2));

  auto all_tasks = replica->all_task_data();
  t.is(all_tasks.size(), (size_t)2, "now there are 2 tasks");
  for (auto& maybe_task : all_tasks) {
    t.ok(maybe_task.is_some(), "all_tasks is fully populated");
    auto task = maybe_task.take();
    if (task->get_uuid() == uuid) {
      t.ok(task->get("description", str), "get_value succeeds");
      t.is(str, "a cool task", "description is 'a cool task'");
    }
  }

  // Check error message formatting for a failed replica open.
  try {
    tc::new_replica_powersync("/does/not/exist/test.db", "00000000-0000-0000-0000-000000000000");
  } catch (rust::Error& err) {
    t.ok(std::string(err.what()).find("failed to open PowerSync DB") != std::string::npos,
         "error message has context about failed open");
  }

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
