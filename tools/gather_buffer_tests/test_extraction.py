# SPDX-License-Identifier: GPL-3.0-or-later
"""Check func without invoking the compiler or running the test generator."""
import ast
from pathlib import Path
import unittest

tree = ast.parse(Path(__file__).with_name('run_tests.py').read_text(encoding='utf-8'))
definition = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'func')
code = compile(ast.Module(body=[definition], type_ignores=[]), '<production func>', 'exec')


class ExtractionTests(unittest.TestCase):
    def extract(self, source, marker='BufferCache<P>::Test('):
        namespace = {'s': source}
        exec(code, namespace)
        return namespace['func'](marker)

    def test_missing_marker(self):
        with self.assertRaisesRegex(SystemExit, r'BufferCache<P>::Test.*marker is missing'):
            self.extract('template <class P>\nvoid Other() {\n}\n')

    def test_missing_template(self):
        with self.assertRaisesRegex(SystemExit, r'BufferCache<P>::Test.*template header is missing'):
            self.extract('void BufferCache<P>::Test() {\n}\n')

    def test_preserves_valid_extraction(self):
        function = 'template <class P>\nvoid BufferCache<P>::Test() {\n    work();\n}\n'
        self.assertEqual(self.extract('// preceding content\n' + function + '// following content'), function)


if __name__ == '__main__':
    unittest.main()
