////////////////////////////////////////////////////////////////////////////////
//
// Copyright 2006 - 2021, Tomas Babej, Paul Beckingham, Federico Hernandez.
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

#include <CmdAdd.h>
#include <Context.h>
#include <feedback.h>
#include <format.h>
#include <taskchampion-cpp/lib.h>

////////////////////////////////////////////////////////////////////////////////
CmdAdd::CmdAdd() {
  _keyword = "add";
  _usage = "task          add <mods>";
  _description = "Adds a new task";
  _read_only = false;
  _displays_id = false;
  _needs_gc = false;
  _uses_context = true;
  _accepts_filter = false;
  _accepts_modifications = true;
  _accepts_miscellaneous = false;
  _category = Command::Category::operation;
}

////////////////////////////////////////////////////////////////////////////////
int CmdAdd::execute(std::string& output) {
  // Apply the command line modifications to the new task.
  Task task;

  // the task is empty, but DOM references can refer to earlier parts of the
  // command line, e.g., `task add due:20110101 wait:due`.
  task.modify(Task::modReplace, true);

  // Validate a task for addition. This is stricter than `task.validate`, as any
  // inconsistency is probably user error.
  task.validate_add();

  // Compute position if task has a parent (append to end of parent's children).
  if (task.has("parent") && task.get("parent") != "") {
    auto parent_uuid = task.get("parent");
    auto tm = Context::getContext().tdb2.tree_map();
    auto parent_tc = tc::uuid_from_string(parent_uuid);
    auto nil_uuid = tc::uuid_from_string("00000000-0000-0000-0000-000000000000");
    auto siblings = tm->sibling_positions(parent_tc, false, nil_uuid, false);
    std::string last_pos;
    if (!siblings.empty()) last_pos = static_cast<std::string>(siblings.back().value);
    auto new_pos = tc::tc_append_position(last_pos);
    task.set("position", static_cast<std::string>(new_pos));
  }

  Context::getContext().tdb2.add(task);

  // Do not display ID 0, users cannot query by that
  auto status = task.getStatus();

  // We may have a situation where both new-id and new-uuid config
  // variables are set. In that case, we'll show the new-uuid, as
  // it's enduring and never changes, and it's unlikely the caller
  // asked for this if they just wanted a human-friendly number.

  std::string shortUuid = task.get("uuid").substr(0, 8);
  std::string parentSuffix;
  if (task.has("parent") && task.get("parent") != "") {
    Task parent_task;
    if (Context::getContext().tdb2.get(task.get("parent"), parent_task))
      parentSuffix = format(" (child of '{1}')", parent_task.get("description"));
  }

  if (Context::getContext().verbose("new-uuid") ||
      (Context::getContext().verbose("new-id") &&
       (status == Task::completed || status == Task::deleted)))
    output += format("Created task {1}{2}.\n", shortUuid, parentSuffix);

  else if (Context::getContext().verbose("new-id") &&
           (status == Task::pending || status == Task::waiting))
    output += format("Created task {1}{2}.\n", shortUuid, parentSuffix);

  if (Context::getContext().verbose("project"))
    Context::getContext().footnote(onProjectChange(task));

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
