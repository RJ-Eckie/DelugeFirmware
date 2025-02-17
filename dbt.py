#!/usr/bin/env python

import argparse
import importlib
import os
import sys
import textwrap
from shutil import copytree
from pathlib import Path

PROG_NAME = sys.argv[0].split(".")[0]

SCRIPTS_DIR = Path("./scripts")
TASKS_DIR = SCRIPTS_DIR / "tasks"
DBT_DEBUG_DIR = SCRIPTS_DIR / "debug"

os.environ["DBT_DEBUG_DIR"] = str(DBT_DEBUG_DIR)
os.environ["DELUGE_FW_ROOT"] = str(Path(".").resolve())

if "DBT_TOOLCHAIN_PATH" not in os.environ:
    os.environ["DBT_TOOLCHAIN_PATH"] = os.environ["DELUGE_FW_ROOT"]

sys.path.append(str(TASKS_DIR))
sys.path.insert(0, str(SCRIPTS_DIR))


def setup_ide_configs():
    if not os.path.exists(".vscode"):
        copytree("IDE_Configs/vscode", ".vscode")
    if not os.path.exists(".idea"):
        copytree("IDE_Configs/clion", ".idea")


def print_tasks_usage(tasks):
    grouped = {}
    for name, stem in tasks.items():
        try:
            module = importlib.import_module(stem)
            argparser = module.argparser()
        except:  # noqa: E722
            argparser = argparse.ArgumentParser(
                prog=name, description="FAILED TO LOAD MODULE"
            )
        try:
            group = argparser.group
        except AttributeError:
            group = "Ungrouped"
        grouped[group] = grouped.get(group, []) + [argparser]

    for group, argparsers in sorted(grouped.items()):
        print(textwrap.indent(group + ":", " " * 2))
        for argparser in argparsers:
            # get our argparsers (lazy import)
            usage: str = argparser.format_usage().strip().removeprefix("usage: ")
            # usage = f"{PROG_NAME} " + usage

            print(textwrap.indent(usage, " " * 4))
            if argparser.description:
                print(textwrap.indent(argparser.description, " " * 6))


def print_help(argparser: argparse.ArgumentParser, tasks: dict):
    argparser.print_help()
    print("")
    print("subcommands: ")
    print_tasks_usage(tasks)


def main() -> int:
    # Create the main parser
    parser = argparse.ArgumentParser(
        prog=f"{PROG_NAME}" or "task",
        add_help=False,
    )

    parser.add_argument(
        "-h", "--help", help="print this help message", action="store_true"
    )
    parser.add_argument("subcommand", nargs="?", metavar="<subcommand>")

    # Specify the folder containing the task files
    task_files = TASKS_DIR.glob("task-*.py")

    # copy vscode config to .vscode if it doesn't exist
    setup_ide_configs()

    tasks = {}
    for task_file in task_files:
        task = task_file.stem
        task_name = task.replace("task-", "")
        tasks[task_name] = task

    # is the subcommand in our list of tasks?
    if len(sys.argv) > 1 and sys.argv[1] in tasks:
        task_name = sys.argv[1]

        # Remove our own program name/path from the arguments
        if sys.argv[1:]:
            sys.argv = sys.argv[1:]

        # Call out to our task. (lazy import)
        retcode = importlib.import_module(tasks[task_name]).main()
        sys.exit(retcode)

    args = parser.parse_args()

    # nothing on the command line
    if args.help or len(sys.argv) == 1:
        print_help(parser, tasks)
        exit()

    if args.subcommand not in tasks:
        print(f"{PROG_NAME}: '{args.subcommand}' is not a valid subcommand.")
        print("")
        print("Valid subcommands:")
        print_tasks_usage(tasks)


if __name__ == "__main__":
    try:
        retcode = main()
        sys.exit(retcode)
    except KeyboardInterrupt:
        sys.exit(130)
