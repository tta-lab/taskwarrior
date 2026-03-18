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

#include <CmdModify.h>
#include <Context.h>
#include <Filter.h>
#include <feedback.h>
#include <format.h>
#include <shared.h>

#include <iostream>

////////////////////////////////////////////////////////////////////////////////
CmdModify::CmdModify() {
  _keyword = "modify";
  _usage = "task <filter> modify <mods>";
  _description = "Modifies the existing task with provided arguments.";
  _read_only = false;
  _displays_id = false;
  _needs_gc = false;
  _uses_context = false;
  _accepts_filter = true;
  _accepts_modifications = true;
  _accepts_miscellaneous = false;
  _category = Command::Category::operation;
}

////////////////////////////////////////////////////////////////////////////////
int CmdModify::execute(std::string&) {
  auto rc = 0;

  // Apply filter.
  Filter filter;
  std::vector<Task> filtered;
  filter.subset(filtered);
  if (filtered.size() == 0) {
    Context::getContext().footnote("No tasks specified.");
    return 1;
  }

  // Accumulated project change notifications.
  std::map<std::string, std::string> projectChanges;

  auto count = 0;
  if (filtered.size() > 1) {
    feedback_affected("This command will alter {1} tasks.", filtered.size());
  }
  for (auto& task : filtered) {
    Task before(task);
    task.modify(Task::modReplace);

    if (before != task) {
      // Abort if change introduces inconsistencies.
      checkConsistency(before, task);

      auto question =
          format("Modify task {1} '{2}'?", task.identifier(true), task.get("description"));

      if (permission(before.diff(task) + question, filtered.size())) {
        count += modifyAndUpdate(before, task, &projectChanges);
      } else {
        std::cout << "Task not modified.\n";
        rc = 1;
        if (_permission_quit) break;
      }
    }
  }

  // Now list the project changes.
  for (const auto& change : projectChanges)
    if (change.first != "") Context::getContext().footnote(change.second);

  feedback_affected(count == 1 ? "Modified {1} task." : "Modified {1} tasks.", count);
  return rc;
}

////////////////////////////////////////////////////////////////////////////////
void CmdModify::checkConsistency(Task& before, Task& after) {
  if ((before.getStatus() == Task::pending) && (after.getStatus() == Task::pending) &&
      (before.get("end") == "") && (after.get("end") != ""))
    throw format("Could not modify task {1}. You cannot set an end date on a pending task.",
                 before.identifier(true));
}

////////////////////////////////////////////////////////////////////////////////
int CmdModify::modifyAndUpdate(Task& before, Task& after,
                               std::map<std::string, std::string>* projectChanges /* = NULL */) {
  auto count = 1;

  feedback_affected("Modifying task {1} '{2}'.", after);
  feedback_unblocked(after);
  Context::getContext().tdb2.modify(after);
  if (Context::getContext().verbose("project") && projectChanges)
    (*projectChanges)[after.get("project")] = onProjectChange(before, after);

  return count;
}

////////////////////////////////////////////////////////////////////////////////
