@decorator
def f(x: int) -> str:
    """doc
    string"""
    s = f"x={x}" + r'\d' + 'it\'s'
    return None if x else 1_000.5
