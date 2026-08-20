#!/usr/bin/env python3
"""Execute and verify the playground lesson corpus for p101-test."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

@dataclass(frozen=True)
class CaseResult:
    name: str
    status: str
    expected_exit: int
    actual_exit: int
    report_dir: Path
    message: str
    expected_findings: tuple[str, ...]
    observed_findings: tuple[str, ...]
    problems: tuple[str, ...]


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="test-corpus", description="Execute and verify playground corpus cases through the student workflow.")
    parser.add_argument("-o", "--output", type=Path, help="Output directory. Default: /tmp/p101-tool-playground-corpus-<pid>")
    parser.add_argument("--case", action="append", dest="cases", help="Run only this case name; may be repeated.")
    parser.add_argument("--track", choices=("c", "systems", "network"), help="Run only cases assigned to this playground track.")
    parser.add_argument("--quick", action="store_true", help="Run only the clean and fd-leak cases.")
    parser.add_argument("--keep-going", action="store_true", help="Continue after a failed case.")
    parser.add_argument("--workflow", type=Path, help="Path to student-workflow.sh.")
    parser.add_argument("--playground", type=Path, help="Path to p101-tool-playground executable.")
    parser.add_argument("--fixtures", type=Path, help="Path to the playground fixture repository.")
    parser.add_argument("--skip-html", action="store_true", help="Pass --skip-html to the workflow.")
    parser.add_argument("--skip-bundle", action="store_true", help="Pass --skip-bundle to the workflow.")
    parser.add_argument("--strict", action="store_true", help="Fail on unexpected extra finding IDs, and require per-case opt-in for exit-code amnesty.")
    return parser.parse_args(argv)


def read_json(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError(f"{path} did not contain a JSON object")
    return data


def find_executable(candidates: list[Path | str]) -> Path:
    for candidate in candidates:
        path = candidate if isinstance(candidate, Path) else Path(candidate)
        if path.is_file() and path.stat().st_mode & 0o111:
            return path.resolve()
        resolved = shutil.which(str(candidate))
        if resolved is not None:
            return Path(resolved).resolve()
    raise FileNotFoundError("none of the candidate executables were found: " + ", ".join(str(c) for c in candidates))


def current_playground_candidates(root: Path) -> list[Path | str]:
    candidates: list[Path | str] = []
    last_build = root / ".last-build-dir"
    try:
        build_dir = last_build.read_text(encoding="utf-8").strip()
    except OSError:
        build_dir = ""
    if build_dir:
        candidates.append(root / build_dir / "p101-tool-playground")
    candidates.extend([root / "build-clang/p101-tool-playground", root / "build-clang-22/p101-tool-playground", root / "build-gcc-16/p101-tool-playground", "p101-tool-playground"])
    return candidates


def case_tracks(case: dict[str, Any]) -> set[str]:
    tracks = case.get("tracks", [])
    if isinstance(tracks, list):
        return {str(item) for item in tracks}
    if isinstance(tracks, str):
        return {tracks}
    return set()


def load_scenario_manifest(playground: Path) -> dict[str, str]:
    completed = subprocess.run(
        [str(playground), "-S"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        raise ValueError(
            f"{playground} could not emit its scenario manifest: "
            f"{completed.stderr.strip()}"
        )
    lines = completed.stdout.splitlines()
    if not lines or lines[0] != "P101SCENARIOS\t1":
        raise ValueError(f"{playground} emitted an invalid scenario manifest")
    scenarios: dict[str, str] = {}
    for line_number, line in enumerate(lines[1:], 2):
        fields = line.split("\t")
        if len(fields) != 3 or not fields[0] or not fields[1]:
            raise ValueError(
                f"{playground}: scenario manifest line {line_number} is invalid"
            )
        if fields[0] in scenarios:
            raise ValueError(f"{playground}: duplicate scenario {fields[0]}")
        scenarios[fields[0]] = fields[1]
    if not scenarios:
        raise ValueError(f"{playground} emitted an empty scenario manifest")
    return scenarios


def load_cases(playground: Path, cases_dir: Path, selected: set[str] | None, track: str | None) -> list[dict[str, Any]]:
    cases: list[dict[str, Any]] = []
    scenarios = load_scenario_manifest(playground)
    for expected_path in sorted(cases_dir.glob("*/expected.json")):
        case = read_json(expected_path)
        case["case_dir"] = str(expected_path.parent)
        name = str(case.get("name"))
        scenario = str(case.get("scenario", ""))
        if scenario not in scenarios:
            raise ValueError(f"{expected_path}: unknown scenario {scenario!r}")
        case["scenario_behavior"] = scenarios[scenario]
        if selected is not None and name not in selected:
            continue
        if track is not None and track not in case_tracks(case):
            continue
        if selected is None or name in selected:
            cases.append(case)
    cases.sort(key=lambda item: (int(item.get("lab_order", 1000)), str(item.get("name", ""))))
    return cases


def read_correlated_ids(report_dir: Path) -> set[str]:
    path = report_dir / "runtime" / "analysis" / "correlated-report.json"
    try:
        data = read_json(path)
    except (OSError, json.JSONDecodeError, ValueError):
        return set()
    findings = data.get("findings")
    if not isinstance(findings, list):
        return set()
    ids: set[str] = set()
    for finding in findings:
        if isinstance(finding, dict) and isinstance(finding.get("id"), str):
            ids.add(finding["id"])
    return ids


def correlated_file_ids(path: Path) -> set[str]:
    try:
        data = read_json(path)
    except (OSError, json.JSONDecodeError, ValueError):
        return set()
    findings = data.get("findings")
    if not isinstance(findings, list):
        return set()
    ids: set[str] = set()
    for finding in findings:
        if isinstance(finding, dict) and isinstance(finding.get("id"), str):
            ids.add(finding["id"])
    return ids


def fault_walk_has_findings(report_dir: Path) -> bool:
    fault_dir = report_dir / "fault-walk"
    for path in fault_dir.glob("*.run/analysis/correlated-report.json"):
        if correlated_file_ids(path):
            return True
        try:
            data = read_json(path)
        except (OSError, json.JSONDecodeError, ValueError):
            continue
        summary = data.get("summary")
        if isinstance(summary, dict) and isinstance(summary.get("findings"), int) and summary["findings"] > 0:
            return True
    return False


def doctor_status_code(report_dir: Path, name: str) -> int | None:
    try:
        data = read_json(report_dir / "doctor" / "doctor.json")
    except (OSError, json.JSONDecodeError, ValueError):
        return None
    statuses = data.get("statuses")
    if not isinstance(statuses, dict):
        return None
    value = statuses.get(name)
    if isinstance(value, dict) and isinstance(value.get("code"), int):
        return int(value["code"])
    if isinstance(value, int):
        return value
    return None


def clean_runtime_with_module_findings(report_dir: Path) -> bool:
    return (
        doctor_status_code(report_dir, "p101_module_map") == 1
        and not read_correlated_ids(report_dir)
        and not fault_walk_has_findings(report_dir)
    )


def run_case(root: Path, workflow: Path, playground: Path, out_dir: Path, case: dict[str, Any], skip_html: bool, skip_bundle: bool, strict: bool = False) -> CaseResult:
    name = str(case["name"])
    scenario = str(case["scenario"])
    case_out = out_dir / name
    output_file = case_out / "playground-output.txt"
    log_path = out_dir / "logs" / f"{name}.log"
    expected_exit = int(case["expected_exit"])
    fault_count = int(case.get("fault_count", 0))
    expected_findings = {str(item) for item in case.get("expected_findings", [])}
    allowed_extra_findings = {str(item) for item in case.get("allowed_extra_findings", [])}
    exit_amnesty = str(case.get("exit_amnesty", ""))
    expected_error_path_findings = bool(case.get("expected_error_path_findings", False))
    expected_output_size = case.get("expected_output_size")
    expected_output_contains = [str(item) for item in case.get("expected_output_contains", [])]
    expected_output_missing = [str(item) for item in case.get("expected_output_missing", [])]

    command = [
        str(workflow),
        "--skip-quality",
        "-p",
        str(root),
        "-s",
        "src",
        "-n",
        str(fault_count),
        "-o",
        str(case_out),
    ]
    if skip_html:
        command.append("--skip-html")
    if skip_bundle:
        command.append("--skip-bundle")
    command.extend(["--", str(playground), "-s", scenario, "-o", str(output_file)])

    with log_path.open("w", encoding="utf-8") as log:
        log.write("$ " + " ".join(command) + "\n\n")
        completed = subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=False)

    problems: list[str] = []
    if completed.returncode != expected_exit:
        # Exit-code amnesty is opt-in per case under --strict; outside strict mode
        # it still applies to every case, as before.
        amnesty_requested = exit_amnesty == "module-map" if strict else exit_amnesty in ("", "module-map")
        if not (amnesty_requested and expected_exit == 0 and completed.returncode == 1 and clean_runtime_with_module_findings(case_out)):
            problems.append(f"expected exit {expected_exit}, got {completed.returncode}")

    actual_ids = read_correlated_ids(case_out)
    missing = sorted(expected_findings - actual_ids)
    if missing:
        problems.append("missing finding IDs: " + ", ".join(missing))

    if strict:
        extra = sorted(actual_ids - expected_findings - allowed_extra_findings)
        if extra:
            problems.append("unexpected finding IDs: " + ", ".join(extra))

    if expected_error_path_findings and not fault_walk_has_findings(case_out):
        problems.append("expected error-path findings, but fault walk looked clean")

    if expected_output_size is not None:
        try:
            actual_output_size = output_file.stat().st_size
        except OSError:
            problems.append(f"expected output size {expected_output_size}, but output file was missing")
        else:
            if actual_output_size != int(expected_output_size):
                problems.append(f"expected output size {expected_output_size}, got {actual_output_size}")

    if expected_output_contains or expected_output_missing:
        try:
            output_text = output_file.read_text(encoding="utf-8", errors="replace")
        except OSError:
            problems.append("expected output content, but output file was missing")
        else:
            missing_output = [item for item in expected_output_contains if item not in output_text]
            if missing_output:
                problems.append("missing output text: " + ", ".join(missing_output))
            present_output = [item for item in expected_output_missing if item in output_text]
            if present_output:
                problems.append("unexpected output text present: " + ", ".join(present_output))

    if problems:
        return CaseResult(
            name,
            "FAIL",
            expected_exit,
            completed.returncode,
            case_out,
            "; ".join(problems),
            tuple(sorted(expected_findings)),
            tuple(sorted(actual_ids)),
            tuple(problems),
        )
    return CaseResult(
        name,
        "PASS",
        expected_exit,
        completed.returncode,
        case_out,
        str(case.get("lesson", "")),
        tuple(sorted(expected_findings)),
        tuple(sorted(actual_ids)),
        (),
    )


def write_summary(out_dir: Path, results: list[CaseResult], cases: list[dict[str, Any]]) -> None:
    summary = out_dir / "summary.md"
    lesson_by_name = {str(case["name"]): str(case.get("lesson", "")) for case in cases}
    with summary.open("w", encoding="utf-8") as stream:
        stream.write("# p101-test playground corpus\n\n")
        stream.write("| Status | Case | Expected exit | Actual exit | Report | Lesson |\n")
        stream.write("| --- | --- | ---: | ---: | --- | --- |\n")
        for result in results:
            rel_report = result.report_dir.relative_to(out_dir)
            lesson = lesson_by_name.get(result.name, "")
            stream.write(f"| {result.status} | `{result.name}` | {result.expected_exit} | {result.actual_exit} | [{rel_report}/index.html](./{rel_report}/index.html) | {lesson} |\n")
        stream.write("\n## Logs\n\n")
        for result in results:
            stream.write(f"- `{result.name}`: [logs/{result.name}.log](./logs/{result.name}.log)\n")


def write_receipt(
    out_dir: Path,
    fixtures_root: Path,
    results: list[CaseResult],
    cases: list[dict[str, Any]],
    strict: bool,
) -> bool:
    selected_count = len(cases)
    case_by_name = {str(case["name"]): case for case in cases}
    passed = len(results) == selected_count and all(result.status == "PASS" for result in results)
    document = {
        "schema": "p101-corpus-receipt-v1",
        "passed": passed,
        "strict": strict,
        "fixtures": str(fixtures_root),
        "selected_cases": selected_count,
        "completed_cases": len(results),
        "cases": [
            {
                "name": result.name,
                "scenario": str(case_by_name[result.name]["scenario"]),
                "scenario_behavior": str(case_by_name[result.name]["scenario_behavior"]),
                "status": result.status,
                "expected_exit": result.expected_exit,
                "actual_exit": result.actual_exit,
                "report": result.report_dir.relative_to(out_dir).as_posix(),
                "expected_findings": list(result.expected_findings),
                "observed_findings": list(result.observed_findings),
                "problems": list(result.problems),
            }
            for result in results
        ],
    }
    temporary = out_dir / "receipt.json.tmp"
    receipt = out_dir / "receipt.json"
    temporary.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(receipt)
    return passed


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    workspace_root = Path(__file__).resolve().parents[4]
    root = args.fixtures.resolve() if args.fixtures else workspace_root / "playgrounds"
    cases_dir = root / "corpus" / "cases"
    out_dir = args.output or Path(f"/tmp/p101-tool-playground-corpus-{os.getpid()}")
    out_dir = out_dir.resolve()
    if out_dir.exists():
        print(f"p101 corpus: output path already exists: {out_dir}", file=sys.stderr)
        return 2
    (out_dir / "logs").mkdir(parents=True)

    if args.quick:
        selected = {"clean", "fd-leak"}
    elif args.cases:
        selected = set(args.cases)
    else:
        selected = None

    try:
        playground = args.playground.resolve() if args.playground else find_executable(current_playground_candidates(root))
        cases = load_cases(playground, cases_dir, selected, args.track)
    except (OSError, ValueError) as exc:
        print(f"p101 corpus: {exc}", file=sys.stderr)
        return 2
    if not cases:
        print("p101 corpus: no cases selected", file=sys.stderr)
        return 2

    workflow = args.workflow.resolve() if args.workflow else find_executable([workspace_root / "scripts/runtime/student-workflow.sh"])

    results: list[CaseResult] = []
    for case in cases:
        result = run_case(root, workflow, playground, out_dir, case, args.skip_html, args.skip_bundle, args.strict)
        results.append(result)
        print(f"{result.status}: {result.name} ({result.message})")
        if result.status != "PASS" and not args.keep_going:
            break

    write_summary(out_dir, results, cases)
    passed = write_receipt(out_dir, root, results, cases, args.strict)
    print(f"p101 corpus output: {out_dir}")
    print(f"Summary: {out_dir / 'summary.md'}")
    print(f"Receipt: {out_dir / 'receipt.json'}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
