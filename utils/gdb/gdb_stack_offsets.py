# Utility script/command for gdb to show how much of stack is each function eating
# Usage: `source utils/gdb/gdb_stack_offsets.py` in the gdb console

import gdb


def frame_address(frame):
    return int(frame.read_register("sp").cast(gdb.lookup_type("uint64_t")))


def invoke():
    frame = gdb.selected_frame()

    # There might be a task + an interrupt on the stack, treat them separately
    while True:
        end_frame = frame

        # Detect interrupt/task frame jump
        while older_frame := end_frame.older():
            end_frame = older_frame

            if (end_frame.name() or "").endswith("_IRQHandler"):
                break

        stack_bottom = frame_address(end_frame)

        while True:
            txt = "{lvl:>4} {offset:>4} {rel_offset:>4} {func:<64}".format(
                lvl=frame.level(),
                offset=stack_bottom - frame_address(frame),
                rel_offset="+" +
                str((frame_address(frame.older()) -
                     frame_address(frame)) if frame.older() else 0),
                func=(frame.name() or "<unknown>")[:63],
            )

            # Add source file and line if available
            sal = frame.find_sal()
            if sal and sal.symtab and sal.line:
                txt += f" at {sal.symtab.filename}:{sal.line}"

            print(txt)

            if frame == end_frame:
                break

            # Move to the calling frame
            frame = frame.older()

        frame = frame.older()
        if not frame:
            break

        # Print an empty line to separate the IRQ from the task
        print()


invoke()
