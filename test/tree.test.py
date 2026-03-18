#!/usr/bin/env python3
###############################################################################
#
# Copyright 2006 - 2021, Tomas Babej, Paul Beckingham, Federico Hernandez.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
# https://www.opensource.org/licenses/mit-license.php
#
###############################################################################

import sys
import os
import unittest

# Ensure python finds the local simpletap module
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

from basetest import Task, TestCase


def get_uuid(t, description):
    """Get UUID of a task by description via export."""
    tasks = t.export()
    for task in tasks:
        if task.get("description") == description:
            return task["uuid"]
    raise AssertionError(f"Task '{description}' not found")


class TestTreeAdd(TestCase):
    """Tests for adding tasks with parent relationships."""

    def setUp(self):
        self.t = Task()

    def test_add_creates_child_relationship(self):
        """Adding a task with parent: sets parent field."""
        self.t("rc.verbose=new-uuid add Project Alpha")
        parent_uuid = get_uuid(self.t, "Project Alpha")

        self.t(f"rc.verbose=new-uuid add Research parent:{parent_uuid}")
        child_tasks = self.t.export()
        child = next(
            (t for t in child_tasks if t.get("description") == "Research"), None
        )
        self.assertIsNotNone(child)
        self.assertEqual(child.get("parent"), parent_uuid)

    def test_add_parent_sets_position(self):
        """Adding a child task assigns a position."""
        self.t("add Project")
        parent_uuid = get_uuid(self.t, "Project")

        self.t(f"add Task1 parent:{parent_uuid}")
        self.t(f"add Task2 parent:{parent_uuid}")

        tasks = self.t.export()
        task1 = next(t for t in tasks if t.get("description") == "Task1")
        task2 = next(t for t in tasks if t.get("description") == "Task2")

        self.assertIn("position", task1)
        self.assertIn("position", task2)
        # Task2 added after Task1 should sort later lexicographically
        self.assertGreater(task2["position"], task1["position"])

    def test_add_output_shows_short_uuid(self):
        """CmdAdd output shows 8-char hex UUID."""
        code, out, err = self.t("rc.verbose=new-uuid add Buy milk")
        # Should show 8-char UUID in output
        import re
        self.assertRegex(out, r"Created task [0-9a-f]{8}\.")


class TestTreeDisplay(TestCase):
    """Tests for task tree display."""

    def setUp(self):
        self.t = Task()

    def test_tree_shows_hierarchy(self):
        """task tree shows box-drawing tree output."""
        self.t("add Project")
        parent_uuid = get_uuid(self.t, "Project")
        self.t(f"add Research parent:{parent_uuid}")
        self.t(f"add Implementation parent:{parent_uuid}")

        code, out, err = self.t(f"{parent_uuid[:8]} tree")
        self.assertIn("[" + parent_uuid[:8] + "] Project", out)
        self.assertIn("Research", out)
        self.assertIn("Implementation", out)
        # Box-drawing characters
        self.assertIn("└─", out)

    def test_tree_no_tasks_returns_error(self):
        """task tree with no matching tasks returns 1."""
        code, out, err = self.t.runError("all tree")

    def test_tree_single_task_subtree_mode(self):
        """Filtering to single task shows subtree."""
        self.t("add Root")
        root_uuid = get_uuid(self.t, "Root")
        self.t(f"add Child parent:{root_uuid}")

        code, out, err = self.t(f"{root_uuid[:8]} tree")
        self.assertIn("[" + root_uuid[:8] + "]", out)
        self.assertIn("Child", out)

    def test_tree_shows_done_indicator(self):
        """Completed tasks show [done] in tree output."""
        self.t("add Root")
        root_uuid = get_uuid(self.t, "Root")
        self.t(f"add Child parent:{root_uuid}")
        child_uuid = get_uuid(self.t, "Child")

        self.t(f"{child_uuid[:8]} rc.confirmation=no done")

        code, out, err = self.t(f"{root_uuid[:8]} tree")
        self.assertIn("[done]", out)


class TestTreeValidation(TestCase):
    """Tests for tree validation (cycles, self-parent)."""

    def setUp(self):
        self.t = Task()

    def test_self_parent_rejected(self):
        """A task cannot be its own parent."""
        self.t("add Task A")
        uuid = get_uuid(self.t, "Task A")

        code, out, err = self.t.runError(
            f"{uuid[:8]} modify parent:{uuid}"
        )
        self.assertIn("cannot be its own parent", err + out)

    def test_circular_reference_rejected(self):
        """Circular parent references are rejected."""
        self.t("add Task A")
        self.t("add Task B")
        uuid_a = get_uuid(self.t, "Task A")
        uuid_b = get_uuid(self.t, "Task B")

        # Make B a child of A
        self.t(f"{uuid_b[:8]} modify parent:{uuid_a}")

        # Try to make A a child of B (would create cycle)
        code, out, err = self.t.runError(
            f"{uuid_a[:8]} modify parent:{uuid_b}"
        )
        self.assertIn("Circular reference", err + out)

    def test_nonexistent_parent_rejected(self):
        """Parent task must exist."""
        self.t("add Task A")
        uuid = get_uuid(self.t, "Task A")
        fake_uuid = "00000000-0000-0000-0000-000000000099"

        code, out, err = self.t.runError(
            f"{uuid[:8]} modify parent:{fake_uuid}"
        )
        self.assertIn("does not exist", err + out)


class TestTreeDone(TestCase):
    """Tests for recursive completion."""

    def setUp(self):
        self.t = Task()

    def test_done_completes_descendants(self):
        """Completing a parent auto-completes all descendants."""
        self.t("add Project")
        parent_uuid = get_uuid(self.t, "Project")
        self.t(f"add Task1 parent:{parent_uuid}")
        self.t(f"add Task2 parent:{parent_uuid}")
        task1_uuid = get_uuid(self.t, "Task1")
        task2_uuid = get_uuid(self.t, "Task2")

        self.t(f"rc.confirmation=no {parent_uuid[:8]} done")

        tasks = {t["uuid"]: t for t in self.t.export()}
        self.assertEqual(tasks[parent_uuid]["status"], "completed")
        self.assertEqual(tasks[task1_uuid]["status"], "completed")
        self.assertEqual(tasks[task2_uuid]["status"], "completed")


class TestTreeDelete(TestCase):
    """Tests for recursive deletion."""

    def setUp(self):
        self.t = Task()

    def test_delete_prompts_for_descendants(self):
        """Deleting a parent with children prompts and deletes all."""
        self.t("add Project")
        parent_uuid = get_uuid(self.t, "Project")
        self.t(f"add Child parent:{parent_uuid}")
        child_uuid = get_uuid(self.t, "Child")

        # Confirm both the parent deletion and child deletion
        self.t(f"rc.confirmation=no {parent_uuid[:8]} delete", input="y\ny\n")

        tasks = {t["uuid"]: t for t in self.t.export()}
        self.assertEqual(tasks[parent_uuid]["status"], "deleted")
        self.assertEqual(tasks[child_uuid]["status"], "deleted")


class TestPlanCommand(TestCase):
    """Tests for task plan command."""

    def setUp(self):
        self.t = Task()

    def test_plan_creates_subtasks(self):
        """task plan creates subtasks from markdown headings."""
        self.t("add Project Alpha")
        parent_uuid = get_uuid(self.t, "Project Alpha")

        markdown = "## Research\nLook into solutions.\n## Implementation\nWrite the code.\n"
        code, out, err = self.t(
            f"{parent_uuid[:8]} plan", input=markdown
        )

        tasks = self.t.export()
        descriptions = [t["description"] for t in tasks]
        self.assertIn("Research", descriptions)
        self.assertIn("Implementation", descriptions)

        # Both subtasks should have parent set
        for task in tasks:
            if task["description"] in ("Research", "Implementation"):
                self.assertEqual(task.get("parent"), parent_uuid)

    def test_plan_nested_headings(self):
        """### headings become grandchildren."""
        self.t("add Project")
        parent_uuid = get_uuid(self.t, "Project")

        markdown = "## Phase 1\n### Backend\n### Frontend\n"
        self.t(f"{parent_uuid[:8]} plan", input=markdown)

        tasks = self.t.export()
        phase1 = next(
            (t for t in tasks if t.get("description") == "Phase 1"), None
        )
        self.assertIsNotNone(phase1)

        backend = next(
            (t for t in tasks if t.get("description") == "Backend"), None
        )
        self.assertIsNotNone(backend)
        self.assertEqual(backend.get("parent"), phase1["uuid"])

    def test_plan_replace_removes_existing(self):
        """task plan replace deletes existing children first."""
        self.t("add Project")
        parent_uuid = get_uuid(self.t, "Project")

        # Initial plan
        self.t(f"{parent_uuid[:8]} plan", input="## Old Task\n")
        old_uuid = get_uuid(self.t, "Old Task")

        # Replace with new plan
        self.t(f"{parent_uuid[:8]} plan replace", input="## New Task\n")

        tasks = {t["uuid"]: t for t in self.t.export()}
        # Old task should be deleted
        self.assertEqual(tasks[old_uuid]["status"], "deleted")
        # New task should exist
        new_task = next(
            (t for t in tasks.values() if t.get("description") == "New Task"), None
        )
        self.assertIsNotNone(new_task)

    def test_plan_no_markdown_returns_error(self):
        """task plan with no stdin returns error."""
        self.t("add Project")
        parent_uuid = get_uuid(self.t, "Project")
        code, out, err = self.t.runError(f"{parent_uuid[:8]} plan", input="")


class TestPositionOrdering(TestCase):
    """Tests for fractional-index position ordering."""

    def setUp(self):
        self.t = Task()

    def test_children_have_ordered_positions(self):
        """Children added sequentially have increasing positions."""
        self.t("add Root")
        root_uuid = get_uuid(self.t, "Root")

        self.t(f"add First parent:{root_uuid}")
        self.t(f"add Second parent:{root_uuid}")
        self.t(f"add Third parent:{root_uuid}")

        tasks = self.t.export()
        children = sorted(
            [t for t in tasks if t.get("parent") == root_uuid],
            key=lambda t: t.get("position", "")
        )
        descriptions = [t["description"] for t in children]
        self.assertEqual(descriptions, ["First", "Second", "Third"])


if __name__ == "__main__":
    from simpletap import TAPTestRunner

    unittest.main(testRunner=TAPTestRunner())

# vim: ai sts=4 et sw=4 ft=python
