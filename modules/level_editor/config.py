# config.py

import os

doc_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "doc_classes")

def can_build(env, platform):
    return True


def configure(env):
    pass


def get_doc_classes():
    return ["LevelMap", "LevelBrush"]


def get_doc_path():
    return doc_path
