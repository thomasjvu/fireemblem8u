"""Current ROM rules, exposed to players without changing the match simulation."""

RULES = {
    "version": 1,
    "party": {
        "id": "mirrored-five-v1",
        "size": 5,
        "selection": "fixed_mirrored",
        "roles": ["sword", "axe", "bow", "mage", "healer"],
        "campaign_import": False,
        "level": 10,
        "experience_gain": False,
    },
    "map": {
        "id": "forest-forts-15-v1",
        "width": 15,
        "height": 15,
        "fog_of_war": False,
        "blue_deployment": [[2, y] for y in [3, 5, 7, 9, 11]],
        "red_deployment": [[12, y] for y in [3, 5, 7, 9, 11]],
        "forts": [[3, 7], [11, 7]],
        "castles": [],
    },
    "objective": {
        "id": "elimination",
        "win": "Eliminate all five opposing units, or accept their surrender.",
        "seize_enabled": False,
        "round_limit": 30,
        "round_limit_result": "draw",
        "mutual_elimination_result": "draw",
    },
    "outcomes": {
        "0": "in_progress",
        "1": "blue_win",
        "2": "red_win",
        "3": "draw",
        "4": "aborted",
    },
    "turns": "Alternate whole-army phases; one round includes both armies.",
}
