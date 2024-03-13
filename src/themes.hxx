#pragma once

// nst
#include "types.hxx"

namespace nst::config {

const Theme DEFAULT_THEME{
	"default",
	{
		/* 8 normal colors */
		"black",
		"red3",
		"green3",
		"yellow3",
		/* instead of the original "blue2" use this - it's no that blue any
		 * more but is at least readable when appearing on black background */
		"#4444ff",
		"magenta3",
		"cyan3",
		"gray90",

		/* 8 bright colors */
		"gray50",
		"red",
		"green",
		"yellow",
		"#6c6cff",
		"magenta",
		"cyan",
		"white",
	},
	{
		"#cccccc",
		"#555555",
		"gray90", /* default foreground colour */
		"black"   /* default background colour */
	},
	ColorIndex{258},
	ColorIndex{259},
	ColorIndex{256},
	ColorIndex{257}
};

// see http://st.suckless.org/patches/solarized for the authors and
// documentation of the two solarlized themes.

const Theme SOLARIZED_LIGHT{
	"solarized-light",
	{
		"#eee8d5",  /*  0: black    */
		"#dc322f",  /*  1: red      */
		"#859900",  /*  2: green    */
		"#b58900",  /*  3: yellow   */
		"#268bd2",  /*  4: blue     */
		"#d33682",  /*  5: magenta  */
		"#2aa198",  /*  6: cyan     */
		"#073642",  /*  7: white    */
		"#fdf6e3",  /*  8: brblack  */
		"#cb4b16",  /*  9: brred    */
		"#93a1a1",  /* 10: brgreen  */
		"#839496",  /* 11: bryellow */
		"#657b83",  /* 12: brblue   */
		"#6c71c4",  /* 13: brmagenta*/
		"#586e75",  /* 14: brcyan   */
		"#002b36",  /* 15: brwhite  */
	},
	{},
	ColorIndex{12},
	ColorIndex{8},
	ColorIndex{14},
	ColorIndex{15},
};

const Theme SOLARIZED_DARK{
	"solarized-dark",
	{
		"#073642",  /*  0: black    */
		"#dc322f",  /*  1: red      */
		"#859900",  /*  2: green    */
		"#b58900",  /*  3: yellow   */
		"#268bd2",  /*  4: blue     */
		"#d33682",  /*  5: magenta  */
		"#2aa198",  /*  6: cyan     */
		"#eee8d5",  /*  7: white    */
		"#002b36",  /*  8: brblack  */
		"#cb4b16",  /*  9: brred    */
		"#586e75",  /* 10: brgreen  */
		"#657b83",  /* 11: bryellow */
		"#839496",  /* 12: brblue   */
		"#6c71c4",  /* 13: brmagenta*/
		"#93a1a1",  /* 14: brcyan   */
		"#fdf6e3",  /* 15: brwhite  */
	},
	/* actually unused, but we need to fill the aeay */
	{},
	ColorIndex{12},
	ColorIndex{8},
	ColorIndex{14},
	ColorIndex{15},
};

// see http://st.suckless.org/patches/nordtheme for the documentation and
// authors of this theme.
//
// "Inspired by the beauty of the arctic, the colors reflect the cold, yet
// harmonious world of ice and the colorfulness of the Aurora Borealis."

const Theme NORDTHEME{
	"nordtheme",
	{
		"#3b4252", /* black   */
		"#bf616a", /* red     */
		"#a3be8c", /* green   */
		"#ebcb8b", /* yellow  */
		"#81a1c1", /* blue    */
		"#b48ead", /* magenta */
		"#88c0d0", /* cyan    */
		"#e5e9f0", /* white   */
	        "#4c566a", /* black   */
		"#bf616a", /* red     */
		"#a3be8c", /* green   */
		"#ebcb8b", /* yellow  */
		"#81a1c1", /* blue    */
		"#b48ead", /* magenta */
		"#8fbcbb", /* cyan    */
		"#eceff4", /* white   */
	},
	{
		"#2e3440", /* background */
		"#d8dee9", /* foreground */
	},
	ColorIndex{257},
	ColorIndex{256},
	ColorIndex{257},
	ColorIndex{256},
};

// see http://st.suckless.org/patches/moonfly/ for documentation and the
// authors of the theme.
//
// Moonfly is a dark color scheme for Vim and Neovim made by bluz71.

const Theme MOONFLY{
	"moonfly",
	{
		"#323437",
		"#ff5454",
		"#8cc85f",
		"#e3c78a",
		"#80a0ff",
		"#d183e8",
		"#79dac8",
		"#a1aab8",
		"#7c8f8f",
		"#ff5189",
		"#36c692",
		"#bfbf97",
		"#74b2ff",
		"#ae81ff",
		"#85dc85",
		"#e2637f"
	},
	{
		"#282a36",
		"#f8f8f2",
		"#080808",
		"#eeeeee",
	},
	ColorIndex{259},
	ColorIndex{258},
	ColorIndex{256},
	ColorIndex{257}
};

// Cyberpunk-Neon is a color scheme made by Roboron3042.
//
// see http://st.suckless.org/patches/cyberpunk-neon/
const Theme CYBERPUNK_NEON{
	"cyberpunk-neon",
	{
		"#123e7c",
		"#ff0000",
		"#d300c4",
		"#f57800",
		"#123e7c",
		"#711c91",
		"#0abdc6",
		"#d7d7d5",
		"#1c61c2",
		"#ff0000",
		"#d300c4",
		"#f57800",
		"#00ff00",
		"#711c91",
		"#0abdc6",
		"#d7d7d5"
	},
	{
		"#0abdc6", // foreground
		"#000b1e", // background
		"#ffffff", // cursor
	},
	ColorIndex{256},
	ColorIndex{257},
	ColorIndex{258},
	ColorIndex{258}
};

// see http://st.suckless.org/patches/dracula/ about the authors and
// documentation of this theme.
//
// Dracula is a color scheme made by Zeno Rocha based on Solarized.

const Theme DRACULA{
	"dracula",
	{
		"#000000", /* black   */
		"#ff5555", /* red     */
		"#50fa7b", /* green   */
		"#f1fa8c", /* yellow  */
		"#bd93f9", /* blue    */
		"#ff79c6", /* magenta */
		"#8be9fd", /* cyan    */
		"#bbbbbb", /* white   */
		"#44475a", /* black   */
		"#ff5555", /* red     */
		"#50fa7b", /* green   */
		"#f1fa8c", /* yellow  */
		"#bd93f9", /* blue    */
		"#ff79c6", /* magenta */
		"#8be9fd", /* cyan    */
		"#ffffff", /* white   */
	},
	{
		"#282a36", /* background */
		"#f8f8f2", /* foreground */
	},
	ColorIndex{257},
	ColorIndex{256},
	ColorIndex{257},
	ColorIndex{257}
};

// see http://st.suckless.org/patches/gruvbox/ about the authors and
// documentaton of this theme.
//
// gruvbox is a retro groove color scheme made by morhetz
const Theme GRUVBOX{
	"gruvbox",
	{
		"#f9f5d7", /* hard contrast: #f9f5d7 / soft contrast: #f2e5bc */
		"#cc241d", /* red     */
		"#98971a", /* green   */
		"#d79921", /* yellow  */
		"#458588", /* blue    */
		"#b16286", /* magenta */
		"#689d6a", /* cyan    */
		"#7c6f64", /* white   */
		"#928374", /* black   */
		"#9d0006", /* red     */
		"#79740e", /* green   */
		"#b57614", /* yellow  */
		"#076678", /* blue    */
		"#8f3f71", /* magenta */
		"#427b58", /* cyan    */
		"#3c3836", /* white   */
	},
	{},
	ColorIndex{15},
	ColorIndex{0},
	ColorIndex{15},
	ColorIndex{0}
};

} // end ns
