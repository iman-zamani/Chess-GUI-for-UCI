"""Item response theory for the Elo estimator (2PL), shared by fit/select/validate.

An engine with ability theta solves position i with probability
    P = sigmoid(a_i * (theta - b_i)).
"Solves" means its move loses at most T win-percentage points against the best
move, judged with the stored reference evaluation of every legal move.

Two ways to fit (compared by cross-validation in validate.py):
  * anchored: the calibration engines' abilities are fixed to their ratings,
    theta = (Elo - CENTER) / SCALE, and every position is a one-dimensional
    penalised logistic regression. theta maps back to Elo exactly.
  * joint: a_i, b_i and every theta_k are fitted together (penalised joint
    maximum likelihood); theta then maps to Elo by a least-squares line.
At test time theta is the penalised maximum-likelihood estimate (Newton) and
its standard error comes from the Fisher information.
"""
import math

import numpy as np

CENTER, SCALE = 2500.0, 400.0


def win_pct(cp):
    """Lichess model: centipawns (mover's view) -> expected win %."""
    return 50.0 + 50.0 * (2.0 / (1.0 + math.exp(-0.00368208 * cp)) - 1.0)


def move_win_pct(m):
    """Reference entry {"cp": x} or {"mate": n} -> win % for the mover."""
    if "mate" in m and m["mate"] is not None:
        return 100.0 if m["mate"] > 0 else 0.0
    return win_pct(m["cp"])


def position_table(ref):
    """{move: win%} for one reference record, and the best win%."""
    t = {m["move"]: move_win_pct(m) for m in ref["moves"]}
    return t, (max(t.values()) if t else 0.0)


def win_loss(table, best, move):
    """Win-% points lost by `move` (moves the reference didn't cover count as 100)."""
    if move not in table:
        return 100.0
    return best - table[move]


def sigmoid(z):
    return 1.0 / (1.0 + np.exp(-np.clip(z, -35, 35)))


# ------------------------------------------------------------------ item fits
def fit_items_anchored(Y, M, theta, lam_alpha=0.05, lam_beta=0.5, beta0=1.0, iters=30):
    """Per-item penalised logistic regression of Y (engines x items, 0/1) on theta.

    M masks observed responses. Model: logit P = alpha_i + beta_i * theta, i.e.
    a = beta, b = -alpha / beta. Ridge penalties keep items with few informative
    responses sane (alpha towards 0, beta towards beta0).
    Returns (a, b, alpha, beta).
    """
    n_items = Y.shape[1]
    x = theta[:, None]
    alpha = np.zeros(n_items)
    beta = np.full(n_items, beta0)
    for _ in range(iters):
        p = sigmoid(alpha[None, :] + beta[None, :] * x)
        r = M * (Y - p)
        w = M * p * (1 - p)
        g_a = r.sum(0) - lam_alpha * alpha
        g_b = (r * x).sum(0) - lam_beta * (beta - beta0)
        h_aa = w.sum(0) + lam_alpha
        h_ab = (w * x).sum(0)
        h_bb = (w * x * x).sum(0) + lam_beta
        det = h_aa * h_bb - h_ab * h_ab
        d_a = (h_bb * g_a - h_ab * g_b) / det
        d_b = (h_aa * g_b - h_ab * g_a) / det
        alpha += np.clip(d_a, -5, 5)
        beta += np.clip(d_b, -5, 5)
        if max(np.abs(d_a).max(), np.abs(d_b).max()) < 1e-9:
            break
    a = beta
    with np.errstate(divide="ignore", invalid="ignore"):
        b = np.where(np.abs(beta) > 1e-9, -alpha / beta, np.sign(-alpha) * 1e3)
    return a, b, alpha, beta


def fit_joint(Y, M, lam_theta=0.2, lam_alpha=0.05, lam_beta=0.5, beta0=1.0, rounds=40):
    """Penalised joint ML: alternate item fits and ability fits. Returns (theta, a, b)."""
    # start from each engine's solve rate
    rate = (Y * M).sum(1) / np.maximum(M.sum(1), 1)
    theta = np.log(np.clip(rate, 0.02, 0.98) / (1 - np.clip(rate, 0.02, 0.98)))
    theta = (theta - theta.mean()) / (theta.std() + 1e-9)
    for _ in range(rounds):
        a, b, alpha, beta = fit_items_anchored(Y, M, theta, lam_alpha, lam_beta, beta0, iters=10)
        for k in range(Y.shape[0]):
            theta[k] = estimate_theta(Y[k], M[k], a, b, prior_sd=1.0 / math.sqrt(lam_theta), iters=10)[0]
        theta = (theta - theta.mean()) / (theta.std() + 1e-9)     # fix the scale (identifiability)
    a, b, _, _ = fit_items_anchored(Y, M, theta, lam_alpha, lam_beta, beta0)
    return theta, a, b


# ------------------------------------------------------------------ ability
def estimate_theta(y, m, a, b, prior_mean=0.0, prior_sd=3.0, iters=50):
    """Penalised ML ability for one engine. y, m: responses and mask over items.
    Returns (theta, se) where se comes from the Fisher information (prior included)."""
    th = prior_mean
    prec = 1.0 / (prior_sd * prior_sd)
    for _ in range(iters):
        p = sigmoid(a * (th - b))
        g = (m * a * (y - p)).sum() - prec * (th - prior_mean)
        h = (m * a * a * p * (1 - p)).sum() + prec
        step = float(np.clip(g / h, -2, 2))
        th += step
        if abs(step) < 1e-10:
            break
    p = sigmoid(a * (th - b))
    info = (m * a * a * p * (1 - p)).sum() + prec
    return th, 1.0 / math.sqrt(info)


def item_information(a, b, theta):
    p = sigmoid(a * (theta - b))
    return a * a * p * (1 - p)


def elo_of(theta):
    return CENTER + SCALE * theta


def theta_of(elo):
    return (elo - CENTER) / SCALE


# ------------------------------------------------------------------ data
def load_dataset(engines_json, reference_jsonl, responses_csv, exclude_synthetic=False):
    """Loads everything into arrays.

    Returns dict with: engine ids, families, elos (CCRL), synthetic flags,
    position ids, categories, reference tables, and L = win-% loss matrix
    (engines x positions, NaN where an engine has no answer).
    """
    import csv
    import json
    eng = json.load(open(engines_json))["engines"]
    if exclude_synthetic:
        eng = [e for e in eng if not e.get("synthetic")]
    refs = [json.loads(l) for l in open(reference_jsonl) if l.strip()]
    pos_index = {r["id"]: i for i, r in enumerate(refs)}
    tables = [position_table(r) for r in refs]
    eidx = {e["id"]: i for i, e in enumerate(eng)}
    L = np.full((len(eng), len(refs)), np.nan)
    with open(responses_csv) as f:
        for row in csv.DictReader(f):
            k, i = eidx.get(row["engine"]), pos_index.get(row["position"])
            if k is None or i is None:
                continue
            t, best = tables[i]
            L[k, i] = win_loss(t, best, row["move"]) if row["move"] else 100.0
    return {
        "engines": [e["id"] for e in eng],
        "families": [e["family"] for e in eng],
        "elo": np.array([e["ccrl"]["rating"] for e in eng], float),
        "synthetic": np.array([bool(e.get("synthetic")) for e in eng]),
        "positions": [r["id"] for r in refs],
        "fens": [r["fen"] for r in refs],
        "categories": [r["category"] for r in refs],
        "tables": tables,
        "L": L,
    }


def responses(L, threshold):
    """0/1 solved matrix and observation mask for a win-% loss threshold."""
    M = (~np.isnan(L)).astype(float)
    Y = np.where(M > 0, (np.nan_to_num(L, nan=100.0) <= threshold).astype(float), 0.0)
    return Y, M


# ------------------------------------------------------------------ selection
CATEGORY_GROUPS = {"tactics": "tactics", "defence": "tactics", "positional": "positional", "endgame": "endgame"}
GROUP_SHARE = {"tactics": 0.40, "positional": 0.35, "endgame": 0.25}
# difficulty bins (Elo of b) and their share: the whole range, denser at 2400-3400
B_BINS = [(1000, 1700, 0.08), (1700, 2100, 0.10), (2100, 2400, 0.12), (2400, 2700, 0.18),
          (2700, 3000, 0.20), (3000, 3400, 0.20), (3400, 3800, 0.12)]


def select_items(a, b, categories, n=1000, a_min=0.25, exclude=None):
    """Indices of up to n items: positive, informative a; difficulties spread over
    B_BINS; categories balanced by GROUP_SHARE. Within each cell the most
    discriminating items win; shortfalls are filled by the best remaining items."""
    b_elo = elo_of(b)
    ok = (a >= a_min) & (b_elo >= B_BINS[0][0]) & (b_elo < B_BINS[-1][1])
    if exclude is not None:
        ok &= ~exclude
    groups = np.array([CATEGORY_GROUPS.get(c, "positional") for c in categories])
    chosen = []
    taken = np.zeros(len(a), bool)
    for g, gs in GROUP_SHARE.items():
        for lo, hi, bs in B_BINS:
            cell = np.where(ok & (groups == g) & (b_elo >= lo) & (b_elo < hi) & ~taken)[0]
            want = int(round(n * gs * bs))
            pick = cell[np.argsort(-a[cell])][:want]
            taken[pick] = True
            chosen.extend(pick.tolist())
    rest = np.where(ok & ~taken)[0]
    chosen.extend(rest[np.argsort(-a[rest])][:max(0, n - len(chosen))].tolist())
    return np.array(sorted(chosen[:n]), int)
