import json
from sentence_transformers import SentenceTransformer
from langchain.vectorstores import Chroma
from langchain.schema import Document
from langchain.embeddings import HuggingFaceEmbeddings

# -------------------------------
# STEP 1: Load schema from file
# -------------------------------
def load_schema_from_file(file_path):
    with open(file_path, "r") as f:
        schema = json.load(f)
    return schema

schema = load_schema_from_file("schema.json")

# -------------------------------
# STEP 2: Convert to text chunks
# -------------------------------
docs = []

# Entities
for ent in schema["entities"]:
    props = ", ".join([f"{k} ({v})" for k, v in ent["properties"].items()]) or "no properties"
    text = f"Entity: {ent['label']} with properties: {props}."
    docs.append(Document(page_content=text))

# Relations
for rel in schema["relations"]:
    props = rel.get("properties", {})
    prop_text = " with properties: " + ", ".join([f"{k} ({v})" for k, v in props.items()]) if props else ""
    text = f"Relation: {rel['label']}. Connects {rel['subj_label']} -> {rel['obj_label']}{prop_text}."
    docs.append(Document(page_content=text))

# -------------------------------
# STEP 3: Use HuggingFace Embeddings (local)
# -------------------------------
embedding = HuggingFaceEmbeddings(
    model_name="sentence-transformers/all-MiniLM-L6-v2"
)

# -------------------------------
# STEP 4: Build or load vector DB
# -------------------------------
vector_db = Chroma.from_documents(docs, embedding, persist_directory="./schema_index_local")
vector_db.persist()

# -------------------------------
# STEP 5: Query Interface
# -------------------------------
def query_schema(natural_language_query, top_k=3):
    vector_db = Chroma(persist_directory="./schema_index_local", embedding_function=embedding)
    results = vector_db.similarity_search(natural_language_query, k=top_k)
    return [r.page_content for r in results]

# -------------------------------
# STEP 6: Try a sample query
# -------------------------------
query = "What relation shows a player joining a football team?"
matches = query_schema(query)

print("\nTop relevant schema elements:\n")
for m in matches:
    print("-", m)
