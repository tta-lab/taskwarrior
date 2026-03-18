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

#include <CLI2.h>
#include <CmdPlan.h>
#include <Context.h>
#include <Filter.h>
#include <Task.h>
#include <taskchampion-cpp/lib.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

////////////////////////////////////////////////////////////////////////////////
CmdPlan::CmdPlan() {
  _keyword = "plan";
  _usage = "task <filter> plan";
  _description = "Creates subtasks from markdown headings read from stdin";
  _read_only = false;
  _displays_id = false;
  _needs_gc = false;
  _accepts_filter = true;
  _accepts_modifications = false;
  _accepts_miscellaneous = true;
  _category = Command::Category::operation;
}

////////////////////////////////////////////////////////////////////////////////
// Parse markdown from a string into a list of MarkdownNode items.
// Lines starting with '#' are headings; depth = number of '#' chars, clamped
// so that '#' and '##' both map to depth=1 (direct child).
// Body lines under a heading accumulate into the annotation field.
std::vector<MarkdownNode> CmdPlan::parseMarkdown(const std::string& input) {
  std::vector<MarkdownNode> nodes;
  std::istringstream stream(input);
  std::string line;
  MarkdownNode* current = nullptr;

  while (std::getline(stream, line)) {
    if (line.empty()) continue;

    if (!line.empty() && line[0] == '#') {
      // Count '#' chars for raw depth.
      size_t level = 0;
      while (level < line.size() && line[level] == '#') ++level;

      // depth 1 = direct child (both ## and # treated as 1)
      int depth = (level <= 2) ? 1 : static_cast<int>(level) - 1;

      std::string title = line.substr(level);
      // Trim leading space after '#'s
      size_t start = title.find_first_not_of(' ');
      if (start != std::string::npos) title = title.substr(start);

      nodes.push_back({depth, title, ""});
      current = &nodes.back();
    } else if (current != nullptr) {
      // Body text → append to annotation
      if (!current->annotation.empty()) current->annotation += "\n";
      current->annotation += line;
    }
  }

  return nodes;
}

////////////////////////////////////////////////////////////////////////////////
// Recursively create subtasks for the given parent, using sequential positions.
// nodes is the flat list from parseMarkdown; we process them depth-first.
void CmdPlan::createSubtasks(const std::string& parentUuid,
                             const std::vector<MarkdownNode>& nodes, std::string& output) {
  // Collect direct children (depth == 1 relative to current parent level).
  // We process the flat list respecting nesting by tracking a "stack" approach.
  // We'll use a recursive descent: find all depth-1 nodes at the top level,
  // then for each, find their depth-2 children (shifted by 1), etc.
  // Since parseMarkdown returns depths relative to markdown nesting, we need
  // to find the minimum depth among remaining nodes.

  // Find the minimum depth to treat as "direct children".
  if (nodes.empty()) return;

  int minDepth = nodes[0].depth;
  for (auto& n : nodes) {
    if (n.depth < minDepth) minDepth = n.depth;
  }

  // Collect top-level groups: each group = one top-level node + its children.
  struct Group {
    MarkdownNode node;
    std::vector<MarkdownNode> children;
  };
  std::vector<Group> groups;

  for (size_t i = 0; i < nodes.size(); ++i) {
    if (nodes[i].depth == minDepth) {
      groups.push_back({nodes[i], {}});
    } else if (!groups.empty()) {
      // Shift depth down by minDepth so children start at 1.
      MarkdownNode shifted = nodes[i];
      shifted.depth -= minDepth;
      groups.back().children.push_back(shifted);
    }
  }

  // Generate sequential positions for all top-level nodes.
  auto positions = tc::tc_sequential_positions(groups.size());

  for (size_t i = 0; i < groups.size(); ++i) {
    const auto& g = groups[i];

    // Create the task — set annotation before add to avoid double undo/sync entries.
    Task task;
    task.set("description", g.node.title);
    task.set("parent", parentUuid);
    task.set("position", static_cast<std::string>(positions[i]));
    task.set("status", "pending");
    if (!g.node.annotation.empty()) task.addAnnotation(g.node.annotation);

    Context::getContext().tdb2.add(task);

    std::string uuid = task.get("uuid");
    output += "  Created [" + uuid.substr(0, 8) + "] " + g.node.title + "\n";

    // Recurse into children.
    if (!g.children.empty()) {
      createSubtasks(uuid, g.children, output);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// Render the subtree of parentUuid for confirmation output.
void CmdPlan::renderSubtree(std::string& output, const std::string& parentUuid, int indent) {
  auto children = Context::getContext().tdb2.children(parentUuid);
  for (auto& child : children) {
    std::string uuid = child.get("uuid");
    output += std::string(indent * 2, ' ') + "  [" + uuid.substr(0, 8) + "] " +
              child.get("description") + "\n";
    renderSubtree(output, uuid, indent + 1);
  }
}

////////////////////////////////////////////////////////////////////////////////
int CmdPlan::execute(std::string& output) {
  // Apply filter — must match exactly one task.
  Filter filter;
  std::vector<Task> filtered;
  filter.subset(filtered);

  if (filtered.empty()) {
    Context::getContext().footnote("No tasks specified.");
    return 1;
  }
  if (filtered.size() > 1) {
    Context::getContext().footnote("'plan' requires exactly one task.");
    return 1;
  }

  const Task& parent = filtered[0];
  std::string parentUuid = parent.get("uuid");

  // Check for replace / --replace flag in miscellaneous words.
  bool doReplace = false;
  for (auto& arg : Context::getContext().cli2.getWords()) {
    if (arg == "replace" || arg == "--replace") {
      doReplace = true;
      break;
    }
  }

  // --replace: delete all existing descendants leaf-first.
  if (doReplace) {
    auto descs = Context::getContext().tdb2.descendants(parentUuid);
    // Process in reverse order (leaves first).
    for (int i = static_cast<int>(descs.size()) - 1; i >= 0; --i) {
      Task t = descs[i];
      t.setStatus(Task::deleted);
      Context::getContext().tdb2.modify(t);
    }
  }

  // Read markdown from stdin.
  std::string markdown;
  {
    std::ostringstream buf;
    buf << std::cin.rdbuf();
    if (std::cin.bad() || buf.fail())
      throw std::string("Failed to read markdown from stdin.");
    markdown = buf.str();
  }

  if (markdown.empty()) {
    Context::getContext().footnote("No markdown provided on stdin.");
    return 1;
  }

  // Parse and create.
  auto nodes = parseMarkdown(markdown);
  if (nodes.empty()) {
    Context::getContext().footnote("No headings found in markdown.");
    return 1;
  }

  output += "[" + parentUuid.substr(0, 8) + "] " + parent.get("description") + "\n";
  createSubtasks(parentUuid, nodes, output);

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
