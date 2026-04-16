#pragma once
#include <string>

namespace Prompts {
inline const std::string KNOWLEDGE_EXTRACTION = R"(

You MUST extract **ALL** named entities and construct an RDF (Resource Description Framework) subgraph from this chunk of a larger text corpus.
Do **NOT** skip any triples unless the subject or object is an ambiguous pronoun
(he, she, it, they, them, this, that).

Additionally:
You MUST identify entities that are semantically equivalent (aliases, acronyms, alternate spellings, or clearly interchangeable references in context)
and construct additional triples using the predicate:
"semantically_similar"

Output format WITH relation metadata (ONLY when temporal and/or spatial information is explicitly present):
[
   [subject, predicate, object, subjectType, objectType, when, where],
   ...
]

STRICT RULES:
- Extract **every possible factual relation** in the text, even if many.
- Continue until you have processed the **entire chunk** fully.
- Do not stop early.
- Predicates MUST be valid schema.org properties (canonical form only)
  EXCEPT for the special predicate:
  - "semantically_similar"
- Prefer the MOST GENERAL valid schema.org superclass for entity types
  (e.g., Person instead of Actor, Place instead of City)
- Every tuple MUST contain 5 or more fields
- Don't put null for not applicable fields, just leave it empty
- NEVER use pronouns (He, She, It, They, etc.) as subject or object
- Output ONLY valid JSON

RULES FOR semantically_similar:
- Create this edge ONLY if two entities refer to the same real-world entity
  using different surface forms.
- Valid cases include:
  - Acronym and full form (e.g., "IBM" and "International Business Machines")
  - Common aliases (e.g., "USA" and "United States")
  - Alternate spellings
  - Explicit equivalence statements in text
- DO NOT create similarity edges for:
  - Related but different entities
  - Parent-child relationships
  - Broad category similarity
  - Loose conceptual similarity
- Do NOT invent equivalence.

WORKED EXAMPLE (FOLLOW EXACTLY):

Input text:
Apple Inc. was founded by Steve Jobs and Steve Wozniak in Cupertino.
Tim Cook is the current CEO of Apple Inc.
International Business Machines (IBM) is a multinational technology company.
Annette Bening played Lady Macbeth in 1984 at the American Conservatory Theatre.

Correct output:
[
  ["Apple Inc.", "founder", "Steve Jobs", "Organization", "Person"],
  ["Apple Inc.", "founder", "Steve Wozniak", "Organization", "Person"],
  ["Apple Inc.", "foundingLocation", "Cupertino", "Organization", "Place"],
  ["Tim Cook", "jobTitle", "CEO", "Person", "DefinedTerm"],
  ["Tim Cook", "worksFor", "Apple Inc.", "Person", "Organization"],
  ["International Business Machines", "alternateName", "IBM", "Organization", "Organization"],
  ["IBM", "semantically_similar", "International Business Machines", "Organization", "Organization"],
  ["Annette Bening", "actor", "Lady Macbeth", "Person", "FictionalCharacter", "1984", "American Conservatory Theatre"],
  ["Annette Bening", "performerIn", "American Conservatory Theatre", "Person", "Organization", "1984", "San Francisco"]
]

)";
}   // namespace Prompts