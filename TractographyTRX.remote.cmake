#-- # Grading Level Criteria Report
#-- EVALUATION DATE: 2026-02-07
#-- EVALUATORS: [<<NO EVALUATOR>>,<<NO EVALUATOR>>]
#--
#-- ## Compliance level 2 star (Alpha code feature API development or niche community/execution environment dependance )
#--   - [X] Compiles for at least 1 niche set of execution envirionments, and perhaps others
#--         (may depend on specific external tools like a java environment, or specific external libraries to work )
#--   - [X] All requirements of Levels 1
#--
#-- ## Compliance Level 1 star (Pre-alpha features under development and code of unknown quality)
#--   - [X] Code complies on at least 1 platform
#--
#-- ### Please document here any justification for the criteria above
#       Initial TRX IO integration, with basic read/write testing.

itk_fetch_module(
  TractographyTRX
  "TRX tractography support with lazy streamline access and a streaming writer.
  See the Insight Journal article: https://insight-journal.org/browse/publication/XXXX."
  MODULE_COMPLIANCE_LEVEL 2
  GIT_REPOSITORY https://github.com/your-org/TractographyTRX.git
  GIT_TAG main
  )
