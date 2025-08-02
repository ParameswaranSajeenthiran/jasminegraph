import json
import pdfplumber
from langchain_text_splitters import RecursiveCharacterTextSplitter
from langchain_community.chat_models import ChatOllama
from langchain_core.prompts import ChatPromptTemplate
from langchain_core.output_parsers import JsonOutputParser

# ----------- Step 1: Extract Text from PDF -------------
def extract_text_from_pdf(pdf_path):
    text = ""
    with pdfplumber.open(pdf_path) as pdf:
        for page in pdf.pages:
            page_text = page.extract_text()
            if page_text:
                text += page_text + "\n"
    return text.strip()

pdf_path = "/home/ubuntu/software/jasminegraph/src_python/graph-rag/documents/Ravindu Weerakoon_CV.pdf"
pdf_text = extract_text_from_pdf(pdf_path)
print(pdf_text)

# ----------- Step 2: Chunking -------------
text_splitter = RecursiveCharacterTextSplitter(chunk_size=1000, chunk_overlap=10)
chunks = text_splitter.create_documents([pdf_text])

# ----------- Step 3: Setup LLM Chain -------------
llm = ChatOllama(model="llama3")

prompt = ChatPromptTemplate.from_messages([
    ("system",
     "You are a highly skilled information extractor specialized in knowledge graph construction. "
     "Your job is to extract all possible meaningful subject-predicate-object triples from the given text. "
     "Each triple should be output as a JSON object representing a relationship edge with detailed source and destination nodes and properties."),
    ("human",
     """
Extract all possible subject-predicate-object triples from the following text. 
Output each triple as a JSON object in the following format:

[
  {{
    "source": {{
      "id": "<unique_node_id>",
      "properties": {{
        "id": "<unique_node_id>",
        "label": "<EntityType>",
        "name": "<EntityName>"
      }}
    }},
    "destination": {{
      "id": "<unique_node_id>",
      "properties": {{
        "id": "<unique_node_id>",
        "label": "<EntityType>",
        "name": "<EntityName>"
      }}
    }},
    "properties": {{
      "id": "<unique_relationship_id>",
      "type": "<Predicate>",
      "description": "<Human-readable description of the triple>"
    }}
  }}
]

Requirements:
- Use consistent unique IDs for nodes and relationships.
- Extract all relevant attributes for both source and destination nodes.
- Respond a **JSON array only**, no extra text like "Here are the triples:" or "The triples are:".

Text:
\"\"\"{input}\"\"\"
""")
])


parser = JsonOutputParser()
chain = prompt | llm | parser

# ----------- Step 4: Node Management -------------
node_id_map = {}
node_counter = 10
edge_counter = 0
jsonl_lines = []

def get_node_id(name):
    global node_counter
    if name not in node_id_map:
        node_id_map[name] = str(node_counter)
        node_counter += 1
    return node_id_map[name]

def build_node(node_data):
    node_id = get_node_id(node_data["properties"]["name"])
    properties = {
        "id": node_id,
        "label": node_data["properties"].get("label", "Entity"),
        "name": node_data["properties"]["name"]
    }
    # Add additional attributes
    for key, value in node_data["properties"].items():
        if key not in properties:
            properties[key] = value
    return {
        "id": node_id,
        "properties": properties
    }

# ----------- Step 5: Process Each Chunk -------------
for doc in chunks:
    try:
        triples = chain.invoke({"input": doc.page_content})
        print(f"✅ Extracted {len(triples)} triples from a chunk.")
    except Exception as e:
        print(f"❌ Error processing chunk: {e}")
        continue

    for triple in triples:
        try:
            src_node = build_node(triple["source"])
            dst_node = build_node(triple["destination"])
            rel_props = triple["properties"]

            # Update relationship ID and description if missing
            rel_props["id"] = str(edge_counter)
            rel_props.setdefault("description", f"{src_node['properties']['name']} {rel_props['type']} {dst_node['properties']['name']}.")
            edge_counter += 1

            record = {
                "source": src_node,
                "destination": dst_node,
                "properties": rel_props
            }

            jsonl_lines.append(record)
        except Exception as ex:
            print(f"⚠️ Skipping invalid triple: {ex}")
            continue

# ----------- Step 6: Save as JSONL -------------
with open("rich_knowledge_graph.jsonl", "a") as f:
    for line in jsonl_lines:
        f.write(json.dumps(line) + "\n")

print(f"✅ Appended {len(jsonl_lines)} rich knowledge triples to 'rich_knowledge_graph.jsonl'")
