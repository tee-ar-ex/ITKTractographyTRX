project = "TractographyTRX"
copyright = "NumFOCUS"
author = "ITK Community"

extensions = [
    "sphinx.ext.intersphinx",
    "sphinx_copybutton",
]

html_theme = "furo"
html_theme_options = {
    "sidebar_hide_name": False,
}

intersphinx_mapping = {
    "itk": ("https://itk.org/ITKSoftwareGuide/html", None),
}

copybutton_prompt_text = r"[$#] "
copybutton_prompt_is_regexp = True
