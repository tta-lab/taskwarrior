import atexit
import errno
import json
import os
import shlex
import shutil
import sqlite3
import tempfile
import unittest
from .exceptions import CommandError
from .hooks import Hooks
from .utils import (
    run_cmd_wait,
    run_cmd_wait_nofail,
    which,
    task_binary_location,
    CMAKE_BINARY_DIR,
)
from .compat import STRING_TYPE

# Fixed test user UUID for PowerSync storage
TEST_USER_ID = "00000000-0000-0000-0000-000000000000"


class Task(object):
    """Manage a task warrior instance

    A temporary folder is used as data store of task warrior.
    This class can be instanciated multiple times if multiple taskw clients are
    needed.

    A taskw client should not be used after being destroyed.
    """

    DEFAULT_TASK = task_binary_location()

    def __init__(self, taskw=DEFAULT_TASK):
        """Initialize a Task warrior (client).  The task client runs in a temporary folder.

        :arg taskw: Task binary to use as client (defaults: task in PATH)
        """
        self.taskw = taskw

        # Used to specify what command to launch (and to inject faketime)
        self._command = [self.taskw]

        # Configuration of the isolated environment
        self._original_pwd = os.getcwd()
        self.datadir = tempfile.mkdtemp(prefix="task_")
        self.db_path = os.path.join(self.datadir, "powersync.db")

        # rc overrides accumulated by config() calls, applied as rc.<key>:<value> args
        self._rc_overrides = {
            "news.version": "2.6.0",
        }

        self._init_test_db()

        # Ensure any instance is properly destroyed at session end
        atexit.register(lambda: self.destroy())

        self.reset_env()

        # Hooks disabled until requested
        self.hooks = None

    def _init_test_db(self):
        """Create PowerSync schema tables and views in a fresh SQLite database.

        The PowerSync backend checks for tc_tasks as a VIEW (not a table),
        so we create tc_tasks_data as the backing table, tc_tasks as a view,
        and INSTEAD OF triggers to make the view writable.
        """
        conn = sqlite3.connect(self.db_path)
        conn.executescript("""
            CREATE TABLE IF NOT EXISTS tc_tasks_data (
                id TEXT PRIMARY KEY, user_id TEXT, data TEXT NOT NULL DEFAULT '{}',
                entry_at TEXT, status TEXT, description TEXT, priority TEXT,
                modified_at TEXT, due_at TEXT, scheduled_at TEXT, start_at TEXT,
                end_at TEXT, wait_at TEXT, parent_id TEXT, position TEXT, project_id TEXT
            );
            CREATE VIEW IF NOT EXISTS tc_tasks AS
                SELECT id, user_id, data, entry_at, status, description, priority,
                       modified_at, due_at, scheduled_at, start_at, end_at, wait_at,
                       parent_id, position, project_id
                FROM tc_tasks_data;
            CREATE TRIGGER IF NOT EXISTS tc_tasks_insert
                INSTEAD OF INSERT ON tc_tasks BEGIN
                    INSERT OR REPLACE INTO tc_tasks_data
                        (id, user_id, data, entry_at, status, description, priority,
                         modified_at, due_at, scheduled_at, start_at, end_at, wait_at,
                         parent_id, position, project_id)
                    VALUES (NEW.id, NEW.user_id, COALESCE(NEW.data, '{}'), NEW.entry_at,
                            NEW.status, NEW.description, NEW.priority, NEW.modified_at,
                            NEW.due_at, NEW.scheduled_at, NEW.start_at, NEW.end_at,
                            NEW.wait_at, NEW.parent_id, NEW.position, NEW.project_id);
                END;
            CREATE TRIGGER IF NOT EXISTS tc_tasks_update
                INSTEAD OF UPDATE ON tc_tasks BEGIN
                    UPDATE tc_tasks_data SET
                        user_id = NEW.user_id, data = COALESCE(NEW.data, '{}'),
                        entry_at = NEW.entry_at, status = NEW.status,
                        description = NEW.description, priority = NEW.priority,
                        modified_at = NEW.modified_at, due_at = NEW.due_at,
                        scheduled_at = NEW.scheduled_at, start_at = NEW.start_at,
                        end_at = NEW.end_at, wait_at = NEW.wait_at,
                        parent_id = NEW.parent_id, position = NEW.position,
                        project_id = NEW.project_id
                    WHERE id = OLD.id;
                END;
            CREATE TRIGGER IF NOT EXISTS tc_tasks_delete
                INSTEAD OF DELETE ON tc_tasks BEGIN
                    DELETE FROM tc_tasks_data WHERE id = OLD.id;
                END;
            CREATE TABLE IF NOT EXISTS tc_operations (
                id TEXT PRIMARY KEY, user_id TEXT, data TEXT NOT NULL,
                created_at TEXT DEFAULT (strftime('%Y-%m-%d %H:%M:%f', 'now'))
            );
            CREATE TABLE IF NOT EXISTS projects (
                id TEXT PRIMARY KEY, name TEXT, user_id TEXT,
                created_at TEXT DEFAULT (strftime('%Y-%m-%d %H:%M:%f', 'now'))
            );
            CREATE TABLE IF NOT EXISTS tc_tags (
                id TEXT PRIMARY KEY, task_id TEXT NOT NULL, user_id TEXT,
                name TEXT NOT NULL, UNIQUE (task_id, name)
            );
            CREATE TABLE IF NOT EXISTS tc_annotations (
                id TEXT PRIMARY KEY, task_id TEXT NOT NULL, user_id TEXT,
                entry_at TEXT NOT NULL, description TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS tc_working_set (
                uuid TEXT PRIMARY KEY
            );
        """)
        conn.close()

    def __repr__(self):
        txt = super(Task, self).__repr__()
        return "{0} running from {1}>".format(txt[:-1], self.datadir)

    def __call__(self, *args, **kwargs):
        "aka t = Task() ; t() which is now an alias to t.runSuccess()"
        return self.runSuccess(*args, **kwargs)

    def activate_hooks(self):
        """Enable self.hooks functionality and activate hooks on config"""
        hooks_dir = os.path.join(self.datadir, "hooks")
        os.makedirs(hooks_dir, exist_ok=True)
        self.config("hooks", "1")
        self.config("hooks.location", hooks_dir)
        self.hooks = Hooks(hooks_dir)

    def reset_env(self):
        """Set a new environment derived from the one used to launch the test"""
        # Copy all env variables to avoid clashing subprocess environments
        self.env = os.environ.copy()

        # PowerSync storage config — no TASKRC or TASKDATA needed
        self.env["POWERSYNC_DB_PATH"] = self.db_path
        self.env["POWERSYNC_USER_ID"] = TEST_USER_ID
        self.env.pop("TASKRC", None)
        self.env.pop("TASKDATA", None)

    def config(self, var, value):
        """Set `var` to `value` — stored as rc override applied to every task invocation."""
        self._rc_overrides[var] = value

    def del_config(self, var):
        """Remove `var` from rc overrides."""
        self._rc_overrides.pop(var, None)

    def export(self, export_filter=None):
        """Run "task export", return JSON array of exported tasks."""
        if export_filter is None:
            export_filter = ""

        code, out, err = self.runSuccess(
            "rc.json.array=1 {0} export" "".format(export_filter)
        )

        return json.loads(out)

    def export_one(self, export_filter=None):
        """
        Return a dictionary representing the exported task. Will
        fail if mutliple tasks match the filter.
        """

        result = self.export(export_filter=export_filter)

        if len(result) != 1:
            descriptions = [
                task.get("description") or "[description-missing]" for task in result
            ]

            raise ValueError(
                "One task should match the '{0}' filter, '{1}' "
                "matches:\n    {2}".format(
                    export_filter or "", len(result), "\n    ".join(descriptions)
                )
            )

        return result[0]

    @property
    def latest(self):
        return self.export_one("+LATEST")

    @staticmethod
    def _split_string_args_if_string(args):
        """Helper function to parse and split into arguments a single string
        argument. The string is literally the same as if written in the shell.
        """
        # Enable nicer-looking calls by allowing plain strings
        if isinstance(args, STRING_TYPE):
            args = shlex.split(args)

        return args

    def _rc_override_args(self):
        """Build rc.<key>:<value> argument list from accumulated config overrides."""
        return ["rc.{0}:{1}".format(k, v) for k, v in self._rc_overrides.items()]

    def runSuccess(self, args="", input=None, merge_streams=False, timeout=5):
        """Invoke task with given arguments and fail if exit code != 0

        Use runError if you want exit_code to be tested automatically and
        *not* fail if program finishes abnormally.

        If you wish to pass instructions to task such as confirmations or other
        input via stdin, you can do so by providing a input string.
        Such as input="y\ny\n".

        If merge_streams=True stdout and stderr will be merged into stdout.

        timeout = number of seconds the test will wait for every task call.
        Defaults to 1 second if not specified. Unit is seconds.

        Returns (exit_code, stdout, stderr) if merge_streams=False
                (exit_code, output) if merge_streams=True
        """
        # Create a copy of the command with rc overrides prepended
        command = self._command[:] + self._rc_override_args()

        args = self._split_string_args_if_string(args)
        command.extend(args)

        output = run_cmd_wait_nofail(
            command, input, merge_streams=merge_streams, env=self.env, timeout=timeout
        )

        if output[0] != 0:
            raise CommandError(command, *output)

        return output

    def runError(self, args=(), input=None, merge_streams=False, timeout=5):
        """Invoke task with given arguments and fail if exit code == 0

        Use runSuccess if you want exit_code to be tested automatically and
        *fail* if program finishes abnormally.

        If you wish to pass instructions to task such as confirmations or other
        input via stdin, you can do so by providing a input string.
        Such as input="y\ny\n".

        If merge_streams=True stdout and stderr will be merged into stdout.

        timeout = number of seconds the test will wait for every task call.
        Defaults to 1 second if not specified. Unit is seconds.

        Returns (exit_code, stdout, stderr) if merge_streams=False
                (exit_code, output) if merge_streams=True
        """
        # Create a copy of the command with rc overrides prepended
        command = self._command[:] + self._rc_override_args()

        args = self._split_string_args_if_string(args)
        command.extend(args)

        output = run_cmd_wait_nofail(
            command, input, merge_streams=merge_streams, env=self.env, timeout=timeout
        )

        # output[0] is the exit code
        if output[0] == 0 or output[0] is None:
            raise CommandError(command, *output)

        return output

    def destroy(self):
        """Cleanup the data folder and release server port for other instances"""
        try:
            shutil.rmtree(self.datadir)
        except OSError as e:
            if e.errno == errno.ENOENT:
                # Directory no longer exists
                pass
            else:
                raise

        # Prevent future reuse of this instance
        self.runSuccess = self.__destroyed
        self.runError = self.__destroyed

        # self.destroy will get called when the python session closes.
        # If self.destroy was already called, turn the action into a noop
        self.destroy = lambda: None

    def __destroyed(self, *args, **kwargs):
        raise AttributeError(
            "Task instance has been destroyed. "
            "Create a new instance if you need a new client."
        )

    def diag(self, merge_streams_with=None):
        """Run task diagnostics.

        This function may fail in which case the exception text is returned as
        stderr or appended to stderr if merge_streams_with is set.

        If set, merge_streams_with should have the format:
        (exitcode, out, err)
        which should be the output of any previous process that failed.
        """
        try:
            output = self.runSuccess("diag")
        except CommandError as e:
            # If task diag failed add the error to stderr
            output = (e.code, None, str(e))

        if merge_streams_with is None:
            return output
        else:
            # Merge any given stdout and stderr with that of "task diag"
            code, out, err = merge_streams_with
            dcode, dout, derr = output

            # Merge stdout
            newout = "\n##### Debugging information (task diag): #####\n{0}"
            if dout is None:
                newout = newout.format("Not available, check STDERR")
            else:
                newout = newout.format(dout)

            if out is not None:
                newout = out + newout

            # And merge stderr
            newerr = "\n##### Debugging information (task diag): #####\n{0}"
            if derr is None:
                newerr = newerr.format("Not available, check STDOUT")
            else:
                newerr = newerr.format(derr)

            if err is not None:
                newerr = err + derr

            return code, newout, newerr

    def faketime(self, faketime=None):
        """Set a faketime using libfaketime that will affect the following
        command calls.

        If faketime is None, faketime settings will be disabled.
        """
        cmd = which("faketime")
        if cmd is None:
            raise unittest.SkipTest("libfaketime/faketime is not installed")

        if self._command[0] == cmd:
            self._command = self._command[3:]

        if faketime is not None:
            # Use advanced time format
            self._command = [cmd, "-f", faketime] + self._command

    def make_tc_task(self, **props):
        """Create a task directly in TaskChampion, bypassing TaskWarrior
        entirely, and returning the UUID. The properties are not interpreted by
        the shell.
        """
        # Generate the path to the `make_tc_task` binary, which is a dependency of the
        # test runner.
        make_tc_task = os.path.abspath(
            os.path.join(CMAKE_BINARY_DIR, "test", "make_tc_task")
        )
        cmd = [make_tc_task, self.db_path, TEST_USER_ID]
        for p, v in props.items():
            cmd.append(f"{p}={v}")
        _, out, _ = run_cmd_wait(cmd)
        return out.strip()


# vim: ai sts=4 et sw=4
