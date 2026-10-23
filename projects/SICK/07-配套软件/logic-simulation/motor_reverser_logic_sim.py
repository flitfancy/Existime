from dataclasses import dataclass


@dataclass
class State:
    q: int = 1  # Power-on preset: move right by default.


def hc14(raw: int) -> int:
    return 0 if raw else 1


def vnh_mode(ina: int, inb: int, pwm: int) -> str:
    if not pwm:
        return "OFF / high-Z"
    if ina and not inb:
        return "CW: OUTA=H OUTB=L"
    if not ina and inb:
        return "CCW: OUTA=L OUTB=H"
    if ina and inb:
        return "BRAKE to VCC"
    return "BRAKE to GND"


def step(state: State, left_triggered: bool, right_triggered: bool, label: str) -> dict:
    # NC wiring:
    # normal/closed -> RAW=0, triggered/open/broken -> RAW=1.
    l_raw = 1 if left_triggered else 0
    r_raw = 1 if right_triggered else 0

    l_set_n = hc14(l_raw)
    r_clr_n = hc14(r_raw)

    # D3 pulls DIR_SET_N low whenever L_SET_N is low.
    pre_n = l_set_n
    clr_n = r_clr_n

    invalid_async = False
    if pre_n == 0 and clr_n == 1:
        state.q = 1
    elif pre_n == 1 and clr_n == 0:
        state.q = 0
    elif pre_n == 0 and clr_n == 0:
        # 74HC74 datasheet: PRE and CLR both low is non-stable.
        # The board also makes MOTOR_EN low in this condition, so the bridge is disabled.
        invalid_async = True
    # else hold previous q.

    q = state.q
    nq = 0 if q else 1

    # Diode OR into PWM/MOTOR_EN:
    # enabled when at least one cleaned limit output is high.
    motor_en = 1 if (l_set_n or r_clr_n) else 0

    return {
        "case": label,
        "L_RAW": l_raw,
        "R_RAW": r_raw,
        "L_SET_N": l_set_n,
        "R_CLR_N": r_clr_n,
        "Q/INA": q,
        "nQ/INB": nq,
        "MOTOR_EN/PWM": motor_en,
        "VNH5019 mode": vnh_mode(q, nq, motor_en),
        "note": "74HC74 PRE=CLR=0; H-bridge disabled" if invalid_async else "",
    }


def main() -> None:
    state = State(q=1)
    sequence = [
        ("power-on default", False, False),
        ("right limit opens", False, True),
        ("leaves right limit", False, False),
        ("left limit opens", True, False),
        ("leaves left limit", False, False),
        ("both limits open / both wires broken", True, True),
        ("only left wire broken", True, False),
        ("only right wire broken", False, True),
    ]

    rows = [step(state, left, right, label) for label, left, right in sequence]
    columns = [
        "case",
        "L_RAW",
        "R_RAW",
        "L_SET_N",
        "R_CLR_N",
        "Q/INA",
        "nQ/INB",
        "MOTOR_EN/PWM",
        "VNH5019 mode",
        "note",
    ]

    widths = {
        column: max(len(column), *(len(str(row[column])) for row in rows))
        for column in columns
    }

    print("Behavior-level simulation for SICK_MOTOR_REVERSER_V1")
    print("NC switch model: normal closed = RAW 0, triggered/open/broken = RAW 1")
    print()
    print(" | ".join(column.ljust(widths[column]) for column in columns))
    print("-+-".join("-" * widths[column] for column in columns))
    for row in rows:
        print(" | ".join(str(row[column]).ljust(widths[column]) for column in columns))

    failures = []
    expected = {
        "power-on default": "CW: OUTA=H OUTB=L",
        "right limit opens": "CCW: OUTA=L OUTB=H",
        "leaves right limit": "CCW: OUTA=L OUTB=H",
        "left limit opens": "CW: OUTA=H OUTB=L",
        "leaves left limit": "CW: OUTA=H OUTB=L",
        "both limits open / both wires broken": "OFF / high-Z",
    }
    for row in rows:
        target = expected.get(row["case"])
        if target and row["VNH5019 mode"] != target:
            failures.append(f"{row['case']}: expected {target}, got {row['VNH5019 mode']}")

    print()
    if failures:
        print("FAIL")
        for failure in failures:
            print(f"- {failure}")
        raise SystemExit(1)

    print("PASS: intended forward/reverse/disable behavior matches the V1 logic model.")


if __name__ == "__main__":
    main()
