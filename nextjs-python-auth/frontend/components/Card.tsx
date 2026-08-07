export default function Card({ title, description }: { title: string; description: string }) {
    return (
        <div style={ { border: '1px solid #ccc', borderRadius: 8, padding: 20, marginBottom: 20 } }>
            <h3>{ title }</h3>
            <p>{ description }</p>
        </div>
    );
}