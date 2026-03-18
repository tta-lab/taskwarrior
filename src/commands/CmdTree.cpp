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

#include <CmdTree.h>
#include <Context.h>
#include <Filter.h>
#include <format.h>

#include <iostream>
#include <set>

////////////////////////////////////////////////////////////////////////////////
CmdTree::CmdTree() {
  _keyword = "tree";
  _usage = "task [<filter>] tree";
  _description = "Displays tasks in a tree hierarchy";
  _read_only = true;
  _displays_id = false;
  _needs_gc = true;
  _uses_context = true;
  _accepts_filter = true;
  _accepts_modifications = false;
  _accepts_miscellaneous = false;
  _category = Command::Category::report;
}

////////////////////////////////////////////////////////////////////////////////
// Render one node and recursively render its children.
void CmdTree::renderTree(std::string& output, const rust::Box<tc::TreeMapWrapper>& tree,
                         const std::map<std::string, Task>& taskMap, const std::string& uuid,
                         const std::string& prefix, bool isLast, int depth, int maxDepth) {
  auto it = taskMap.find(uuid);
  if (it == taskMap.end()) return;

  const Task& task = it->second;
  std::string shortUuid = uuid.substr(0, 8);

  // Status indicator for non-pending tasks.
  std::string statusIndicator;
  Task::status status = task.getStatus();
  if (status == Task::completed)
    statusIndicator = " [done]";
  else if (status == Task::deleted)
    statusIndicator = " [del]";

  // Build the line.
  std::string connector = isLast ? "└─ " : "├─ ";
  std::string line = prefix + connector + "[" + shortUuid + "] " + task.get("description") +
                     statusIndicator + "\n";
  output += line;

  // Recurse into children if depth limit not reached.
  if (maxDepth == 0 || depth < maxDepth) {
    tc::Uuid tcUuid = tc::uuid_from_string(uuid);
    auto children = tree->children(tcUuid);
    std::string childPrefix = prefix + (isLast ? "   " : "│  ");
    for (size_t i = 0; i < children.size(); ++i) {
      std::string childUuid = static_cast<std::string>(children[i].to_string());
      bool childIsLast = (i == children.size() - 1);
      renderTree(output, tree, taskMap, childUuid, childPrefix, childIsLast, depth + 1, maxDepth);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
int CmdTree::execute(std::string& output) {
  int maxDepth = Context::getContext().config.getInteger("tree.depth");

  // Apply filter.
  Filter filter;
  std::vector<Task> filtered;
  filter.subset(filtered);

  // Build tree map from TCH.
  auto tree = Context::getContext().tdb2.tree_map();

  // Build a map of all tasks by UUID for fast lookup.
  std::map<std::string, Task> taskMap;
  for (auto& task : Context::getContext().tdb2.all_tasks()) {
    taskMap[task.get("uuid")] = task;
  }

  // Build a set of filter-matched UUIDs.
  std::set<std::string> matchedUuids;
  for (auto& task : filtered) {
    matchedUuids.insert(task.get("uuid"));
  }

  if (filtered.empty()) {
    Context::getContext().footnote("No tasks specified.");
    return 1;
  }

  // Subtree mode: if filter matches exactly one task, show it + all descendants.
  if (filtered.size() == 1) {
    const std::string& rootUuid = filtered[0].get("uuid");
    output += "[" + rootUuid.substr(0, 8) + "] " + filtered[0].get("description") + "\n";

    tc::Uuid tcRoot = tc::uuid_from_string(rootUuid);
    auto children = tree->children(tcRoot);
    std::string childPrefix = "";
    for (size_t i = 0; i < children.size(); ++i) {
      std::string childUuid = static_cast<std::string>(children[i].to_string());
      bool childIsLast = (i == children.size() - 1);
      renderTree(output, tree, taskMap, childUuid, childPrefix, childIsLast, 1, maxDepth);
    }
    return 0;
  }

  // Full tree mode: find roots among the filter-matched tasks.
  // Orphaned children (matched but parent not matched) appear at root level.
  // We render each matched task that has no matched parent as a subtree root.
  std::set<std::string> renderedUuids;

  // Determine which matched tasks are "visual roots" (their parent is not in the matched set).
  std::vector<std::string> visualRoots;
  for (auto& task : filtered) {
    std::string uuid = task.get("uuid");
    std::string parent = task.get("parent");
    if (parent.empty() || matchedUuids.find(parent) == matchedUuids.end()) {
      visualRoots.push_back(uuid);
    }
  }

  // Render each visual root and its descendants (only including matched nodes).
  for (size_t i = 0; i < visualRoots.size(); ++i) {
    const std::string& rootUuid = visualRoots[i];
    if (renderedUuids.count(rootUuid)) continue;

    auto it = taskMap.find(rootUuid);
    if (it == taskMap.end()) continue;

    // Print the root task itself.
    std::string shortUuid = rootUuid.substr(0, 8);
    Task::status status = it->second.getStatus();
    std::string statusIndicator;
    if (status == Task::completed)
      statusIndicator = " [done]";
    else if (status == Task::deleted)
      statusIndicator = " [del]";

    output += "[" + shortUuid + "] " + it->second.get("description") + statusIndicator + "\n";
    renderedUuids.insert(rootUuid);

    // Render children recursively (only matched ones).
    tc::Uuid tcRoot = tc::uuid_from_string(rootUuid);
    auto children = tree->children(tcRoot);
    std::string prefix;
    for (size_t j = 0; j < children.size(); ++j) {
      std::string childUuid = static_cast<std::string>(children[j].to_string());
      if (!matchedUuids.count(childUuid)) continue;
      bool childIsLast = (j == children.size() - 1);
      renderTree(output, tree, taskMap, childUuid, prefix, childIsLast, 1, maxDepth);
      renderedUuids.insert(childUuid);
    }
  }

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
