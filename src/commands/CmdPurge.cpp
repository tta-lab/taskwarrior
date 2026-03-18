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

#include <CmdPurge.h>
#include <Context.h>
#include <Filter.h>
#include <feedback.h>
#include <format.h>
#include <shared.h>

////////////////////////////////////////////////////////////////////////////////
CmdPurge::CmdPurge() {
  _keyword = "purge";
  _usage = "task <filter> purge";
  _description = "Removes the specified tasks from the data files. Causes permanent loss of data.";
  _read_only = false;
  _displays_id = false;
  _needs_confirm = true;
  _needs_gc = true;
  _uses_context = true;
  _accepts_filter = true;
  _accepts_modifications = false;
  _accepts_miscellaneous = false;
  _category = Command::Category::operation;
}

////////////////////////////////////////////////////////////////////////////////
// Purges the task, while taking care of:
// - dependencies on this task
// - pending tree children (blocked — cannot purge if children are still pending)
void CmdPurge::handleRelations(Task& task, std::vector<Task>& tasks) {
  handleDeps(task);
  checkPendingChildren(task);
  tasks.push_back(task);
}

////////////////////////////////////////////////////////////////////////////////
// Makes sure that any task having the dependency on the task being purged
// has that dependency removed, to preserve referential integrity.
void CmdPurge::handleDeps(Task& task) {
  std::string uuid = task.get("uuid");

  for (auto& blockedConst : Context::getContext().tdb2.all_tasks()) {
    Task& blocked = const_cast<Task&>(blockedConst);
    if (blocked.hasDependency(uuid)) {
      blocked.removeDependency(uuid);
      Context::getContext().tdb2.modify(blocked);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// Block purging a task that has pending tree children.
void CmdPurge::checkPendingChildren(Task& task) {
  std::string uuid = task.get("uuid");
  for (auto& childConst : Context::getContext().tdb2.all_tasks()) {
    Task& child = const_cast<Task&>(childConst);
    if (child.get("parent") == uuid &&
        child.getStatus() != Task::deleted &&
        child.getStatus() != Task::completed) {
      throw format(
          "Task '{1}' has pending subtasks — complete or delete them first, or purge them "
          "individually.",
          task.get("description"));
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
int CmdPurge::execute(std::string&) {
  int rc = 0;
  std::vector<Task> tasks;
  bool matched_deleted = false;

  Filter filter;
  std::vector<Task> filtered;

  // Apply filter.
  filter.subset(filtered);
  if (filtered.size() == 0) {
    Context::getContext().footnote("No tasks specified.");
    return 1;
  }

  for (auto& task : filtered) {
    // Allow purging of deleted tasks only. Hence no need to deal with:
    // - unblocked tasks notifications (deleted tasks are not blocking)
    // - project changes (deleted tasks not included in progress)
    // It also has the nice property of being explicit - users need to
    // mark tasks as deleted before purging.
    if (task.getStatus() == Task::deleted) {
      // Mark that at least one deleted task matched the filter
      matched_deleted = true;

      std::string question;
      question = format("Permanently remove task {1} '{2}'?", task.identifier(true),
                        task.get("description"));

      if (permission(question, filtered.size())) handleRelations(task, tasks);
    }
  }

  // Now that any exceptions are handled, actually purge the tasks.
  for (auto& task : tasks) {
    Context::getContext().tdb2.purge(task);
  }

  if (filtered.size() > 0 and !matched_deleted)
    Context::getContext().footnote(
        "No deleted tasks specified. Maybe you forgot to delete tasks first?");

  feedback_affected(tasks.size() == 1 ? "Purged {1} task." : "Purged {1} tasks.", tasks.size());
  return rc;
}

////////////////////////////////////////////////////////////////////////////////
