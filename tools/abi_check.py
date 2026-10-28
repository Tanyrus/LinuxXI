#!/usr/bin/env python3
"""Fail the build when the plugin depends on an Ashita vtable slot that differs between MinGW and MSVC.

GCC lays out virtual functions in declaration order. MSVC does too, except that every overload of a name is pulled
up next to its first declaration. A method that is not overloaded keeps its slot in both layouts unless an overload
group around it is split, so this emulates both layouts and compares slots for every interface method the sources
call. Overloaded and variadic methods are rejected outright, and IPluginBase (whose vtable the plugin builds) must
have neither.

A method that returns a struct by value is rejected too: MSVC member functions return it through a hidden pointer,
while MinGW expects a small one such as ImVec2 back in registers, so Ashita writes the result to a stray address.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SDK = ROOT / 'third_party' / 'ashita-sdk'
SOURCE = ROOT / 'src'
CALLED_INTERFACES = ['IAshitaCore', 'IMemoryManager', 'IEntity', 'IParty', 'IPlayer', 'IRecast', 'IChatManager',
                     'IConfigurationManager', 'IGuiManager', 'ITarget', 'IProperties',
                     'IInputManager', 'IMouse', 'IKeyboard', 'IInventory',
                     'IPacketManager', 'IResourceManager', 'IFontManager', 'IPrimitiveManager',
                     'ICastBar', 'IAutoFollow', 'IPluginManager']
NOT_CALLED = {'ILogManager'}  # handed to the plugin, never called
# Calls are matched by name alone: these share a name with a call on another interface and are never made on this one.
# The plugin's ->GetString calls are IConfigurationManager's; it reads only GetItemById from IResourceManager.
NAME_ONLY_ELSEWHERE = {('IResourceManager', 'GetString')}
INTERFACE = re.compile(r'^(?:struct|interface)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{(.*?)^\};', re.M | re.S)
DECL = re.compile(r'virtual\s+[^;]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)\)\s*(?:const)?\s*=\s*0\s*;')
CALL = re.compile(r'->\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(')
STRUCT = re.compile(r'^\s*struct\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?::[^{;]*)?\{', re.M)
RETURNING = re.compile(r'virtual\s+([^;(]*?)\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*\)\s*(?:const)?\s*=\s*0\s*;')
INTERFACE_POINTER = re.compile(r'\b(I[A-Z][A-Za-z0-9_]*)\s*\*')


def interfaces(text):
    """Interface name -> [(method name, is variadic)] in declaration order."""
    found = {}
    for m in INTERFACE.finditer(text):
        decls = [(d.group(1), '...' in d.group(2)) for d in DECL.finditer(m.group(2))]
        if decls:
            found[m.group(1)] = decls
    return found


def struct_returns(text):
    """Interface name -> names of its methods that return a struct the headers define, by value."""
    structs = set(STRUCT.findall(text))
    found = {}
    for m in INTERFACE.finditer(text):
        names = set()
        for d in RETURNING.finditer(m.group(2)):
            returned = d.group(1)
            words = returned.split()
            if words and words[-1] in structs and '*' not in returned and '&' not in returned:
                names.add(d.group(2))
        if names:
            found[m.group(1)] = names
    return found


def returned_interfaces(text):
    """Interface name -> {method: the interface whose pointer it returns}."""
    found = {}
    for m in INTERFACE.finditer(text):
        for d in RETURNING.finditer(m.group(2)):
            target = INTERFACE_POINTER.fullmatch(d.group(1).strip())
            if target:
                found.setdefault(m.group(1), {})[d.group(2)] = target.group(1)
    return found


def unlisted_interfaces(sources, text, declared, calls, listed):
    """SDK interfaces the sources name as a pointer, or reach through a call on a listed one, that are not listed."""
    used = set(INTERFACE_POINTER.findall(sources))
    for iface, methods in returned_interfaces(text).items():
        if iface in listed:
            used |= {target for method, target in methods.items() if method in calls}
    return sorted((used & set(declared)) - set(listed) - NOT_CALLED)


def msvc_order(decls):
    """Declaration indices in MSVC slot order: overloads grouped at their first declaration."""
    order, seen = [], set()
    for name, _ in decls:
        if name not in seen:
            seen.add(name)
            order += [j for j, (n, _) in enumerate(decls) if n == name]
    return order


def unstable_methods(decls):
    """Method name -> reason it is unsafe to call or implement across the MinGW/MSVC boundary."""
    slot = {decl: s for s, decl in enumerate(msvc_order(decls))}
    counts = {}
    for name, _ in decls:
        counts[name] = counts.get(name, 0) + 1
    bad = {}
    for i, (name, variadic) in enumerate(decls):
        if counts[name] > 1:
            bad[name] = 'overloaded'
        elif variadic:
            bad[name] = 'variadic'
        elif slot[i] != i:
            bad[name] = f'slot {i} under MinGW but {slot[i]} under MSVC'
    return bad


def main() -> int:
    text = (SDK / 'Ashita.h').read_text(errors='replace') + '\n' + (SDK / 'imgui.h').read_text(errors='replace')
    found = interfaces(text)
    problems = []
    if 'IPluginBase' not in found:
        problems.append('IPluginBase not found in the SDK')
    else:
        problems += [f'IPluginBase::{n} ({why})' for n, why in unstable_methods(found['IPluginBase']).items()]
    # Any folder named on the command line, such as a developer's tools when they are built in, is checked too.
    folders = [SOURCE, *(pathlib.Path(arg) for arg in sys.argv[1:])]
    sources = '\n'.join(path.read_text(errors='replace') for folder in folders for pattern in ('*.cpp', '*.h')
                        for path in folder.glob(pattern))
    calls = set(CALL.findall(sources))
    problems += [f'the plugin uses {name}, which is not in CALLED_INTERFACES and so is not checked'
                 for name in unlisted_interfaces(sources, text, found, calls, CALLED_INTERFACES)]
    checked = 0
    returns = struct_returns(text)
    for iface in CALLED_INTERFACES:
        if iface not in found:
            problems.append(f'{iface} not found in the SDK')
            continue
        bad = unstable_methods(found[iface])
        for name in returns.get(iface, ()):
            bad.setdefault(name, 'returns a struct by value')
        for call in sorted(calls & {n for n, _ in found[iface]}):
            if (iface, call) in NAME_ONLY_ELSEWHERE:
                continue
            checked += 1
            if call in bad:
                problems.append(f'{iface}::{call} ({bad[call]})')
    for p in problems:
        print(f'ABI: {p}')
    print(f'abi_check: {checked} interface methods checked, {len(problems)} problems')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
