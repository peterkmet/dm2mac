def test_game_files_exist(game_dir):
    assert (game_dir / "SKULL.EXE").is_file()
    assert (game_dir / "DATA" / "GRAPHICS.DAT").is_file()
