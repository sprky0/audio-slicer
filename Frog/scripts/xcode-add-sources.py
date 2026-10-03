#!/usr/bin/env python3
"""Add source files to every native target of Frog-macOS.xcodeproj.

	python3 scripts/xcode-add-sources.py engine/*.c ui/*.cpp     # from Frog/

Idempotent: a file already referenced is skipped. Paths are relative to
Frog/; the project lives in Frog/projects/, so references get a `../` prefix.
Each file gets one PBXFileReference, is placed in the group that holds
Frog.cpp, and gets one PBXBuildFile per Sources phase (one per target).
"""
import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FROG = os.path.normpath(os.path.join(HERE, '..'))
PBX = os.path.join(FROG, 'projects', 'Frog-macOS.xcodeproj', 'project.pbxproj')

TYPES = {'.c': 'sourcecode.c.c', '.cpp': 'sourcecode.cpp.cpp', '.mm': 'sourcecode.cpp.objcpp', '.m': 'sourcecode.c.objc', '.h': 'sourcecode.c.h'}


def oid(key):
	"""A stable 24-hex object id from a key."""
	return hashlib.sha1(key.encode()).hexdigest()[:24].upper()


def main(argv):
	if not argv:
		print(__doc__)
		return 2
	text = open(PBX).read()
	phases = re.findall(r'^\t\t([0-9A-F]{24}) /\* Sources \*/ = \{\n\t\t\tisa = PBXSourcesBuildPhase;', text, re.M)
	if not phases:
		print('no PBXSourcesBuildPhase found')
		return 1
	# the group holding Frog.cpp
	m = re.search(r'(\t\t\t\t[0-9A-F]{24} /\* Frog\.cpp \*/,\n)', text)
	if not m:
		print('Frog.cpp group entry not found')
		return 1
	group_anchor = m.group(1)
	added = 0
	for arg in argv:
		rel = os.path.relpath(os.path.abspath(arg), FROG)
		name = os.path.basename(rel)
		ext = os.path.splitext(name)[1]
		if ext not in TYPES:
			print(f'skip {rel}: unknown type')
			continue
		if re.search(r'/\* ' + re.escape(name) + r' \*/ = \{isa = PBXFileReference', text):
			print(f'skip {rel}: already referenced')
			continue
		fref = oid('fileref:' + rel)
		# file reference
		ref_line = f'\t\t{fref} /* {name} */ = {{isa = PBXFileReference; fileEncoding = 4; lastKnownFileType = {TYPES[ext]}; name = {name}; path = ../{rel}; sourceTree = "<group>"; }};\n'
		text = text.replace('/* End PBXFileReference section */', ref_line + '/* End PBXFileReference section */', 1)
		# group
		text = text.replace(group_anchor, group_anchor + f'\t\t\t\t{fref} /* {name} */,\n', 1)
		# one build file per Sources phase
		if ext != '.h':
			for ph in phases:
				bf = oid(f'buildfile:{rel}:{ph}')
				bf_line = f'\t\t{bf} /* {name} in Sources */ = {{isa = PBXBuildFile; fileRef = {fref} /* {name} */; }};\n'
				text = text.replace('/* End PBXBuildFile section */', bf_line + '/* End PBXBuildFile section */', 1)
				pat = re.compile(r'(\t\t' + ph + r' /\* Sources \*/ = \{\n\t\t\tisa = PBXSourcesBuildPhase;\n\t\t\tbuildActionMask = \d+;\n\t\t\tfiles = \(\n)')
				text, n = pat.subn(lambda mm: mm.group(1) + f'\t\t\t\t{bf} /* {name} in Sources */,\n', text, count=1)
				if n != 1:
					print(f'warning: phase {ph} not patched for {name}')
		added += 1
		print(f'added {rel} ({len(phases)} targets)')
	open(PBX, 'w').write(text)
	print(f'{added} file(s) added')
	return 0


if __name__ == '__main__':
	sys.exit(main(sys.argv[1:]))
